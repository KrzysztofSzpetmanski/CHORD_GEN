#include "plugin.hpp"
#include "BuildNumber.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <vector>

using namespace rack;

namespace {

constexpr std::array<const char*, 12> ROOT_NAMES = {
	"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

constexpr std::array<const char*, 9> SCALE_NAMES = {
	"Major", "Minor", "Dorian", "Phrygian", "Lydian", "Mixolydian", "Locrian", "Pentatonic", "Blues"
};

constexpr std::array<std::array<int, 7>, 9> SCALE_INTERVALS = {{
	{{0, 2, 4, 5, 7, 9, 11}},
	{{0, 2, 3, 5, 7, 8, 10}},
	{{0, 2, 3, 5, 7, 9, 10}},
	{{0, 1, 3, 5, 7, 8, 10}},
	{{0, 2, 4, 6, 7, 9, 11}},
	{{0, 2, 4, 5, 7, 9, 10}},
	{{0, 1, 3, 5, 6, 8, 10}},
	{{0, 3, 5, 7, 10, 0, 0}},
	{{0, 3, 5, 6, 7, 10, 0}},
}};

constexpr std::array<int, 9> SCALE_LENGTHS = {{7, 7, 7, 7, 7, 7, 7, 5, 6}};
constexpr std::array<const char*, 5> TYPE_NAMES = {{"Diat7", "Sus2", "Sus4", "7", "6"}};

constexpr std::array<std::array<int, 4>, 24> VOICE_PERMUTATIONS = {{
	{{0, 1, 2, 3}}, {{0, 1, 3, 2}}, {{0, 2, 1, 3}}, {{0, 2, 3, 1}},
	{{0, 3, 1, 2}}, {{0, 3, 2, 1}}, {{1, 0, 2, 3}}, {{1, 0, 3, 2}},
	{{1, 2, 0, 3}}, {{1, 2, 3, 0}}, {{1, 3, 0, 2}}, {{1, 3, 2, 0}},
	{{2, 0, 1, 3}}, {{2, 0, 3, 1}}, {{2, 1, 0, 3}}, {{2, 1, 3, 0}},
	{{2, 3, 0, 1}}, {{2, 3, 1, 0}}, {{3, 0, 1, 2}}, {{3, 0, 2, 1}},
	{{3, 1, 0, 2}}, {{3, 1, 2, 0}}, {{3, 2, 0, 1}}, {{3, 2, 1, 0}}
}};

int clampInt(int v, int lo, int hi) {
	if (v < lo) return lo;
	if (v > hi) return hi;
	return v;
}

int degreeCount(int scaleIndex) {
	const int idx = clampInt(scaleIndex, 0, int(SCALE_NAMES.size()) - 1);
	return std::max(1, SCALE_LENGTHS[size_t(idx)]);
}

int pitchClassFromMidi(int midi) {
	int pc = midi % 12;
	if (pc < 0) pc += 12;
	return pc;
}

const char* noteNameFromPitchClass(int pitchClass) {
	const int idx = clampInt(pitchClass, 0, 11);
	return ROOT_NAMES[size_t(idx)];
}

float midiToVolts(int midi) {
	return (float(midi) - 60.f) / 12.f;
}

int voltsToMidi(float volts) {
	const int midi = int(std::lround(volts * 12.f + 60.f));
	return clampInt(midi, 0, 127);
}

std::string midiToNoteName(int midi) {
	const int clamped = clampInt(midi, 0, 127);
	const int pc = pitchClassFromMidi(clamped);
	const int octave = (clamped / 12) - 1;
	return std::string(noteNameFromPitchClass(pc)) + std::to_string(octave);
}

int circularDistance(int a, int b) {
	int d = std::abs(a - b) % 12;
	return std::min(d, 12 - d);
}

int scaleDegreeToSemitone(int scaleIndex, int degree) {
	const int idx = clampInt(scaleIndex, 0, int(SCALE_NAMES.size()) - 1);
	const int len = degreeCount(idx);
	int d = degree;
	int octaveShift = 0;
	while (d < 0) {
		d += len;
		octaveShift -= 1;
	}
	while (d >= len) {
		d -= len;
		octaveShift += 1;
	}
	return SCALE_INTERVALS[size_t(idx)][size_t(d)] + 12 * octaveShift;
}

int intervalFromDegree(int scaleIndex, int fromDegree, int toDegree) {
	return scaleDegreeToSemitone(scaleIndex, toDegree) - scaleDegreeToSemitone(scaleIndex, fromDegree);
}

int scaleDegreeToMidi(int tonicMidi, int scaleIndex, int degree) {
	return tonicMidi + scaleDegreeToSemitone(scaleIndex, degree);
}

int closestScaleDegreeForPitchClass(int rootPc, int scaleIndex, int inputPc) {
	const int len = degreeCount(scaleIndex);
	int bestDegree = 0;
	int bestDist = 999;
	for (int d = 0; d < len; ++d) {
		const int scalePc = (rootPc + scaleDegreeToSemitone(scaleIndex, d)) % 12;
		const int dist = circularDistance(scalePc, inputPc);
		if (dist < bestDist) {
			bestDist = dist;
			bestDegree = d;
		}
	}
	return bestDegree;
}

std::array<bool, 12> makeExpectedDiatonicMask(int scaleIndex, int rootPc, int degree) {
	std::array<bool, 12> expected{};
	const int tonicMidi = 60 + rootPc;
	const int chordRootMidi = scaleDegreeToMidi(tonicMidi, scaleIndex, degree);
	const int i3 = intervalFromDegree(scaleIndex, degree, degree + 2);
	const int i5 = intervalFromDegree(scaleIndex, degree, degree + 4);
	const int i7 = intervalFromDegree(scaleIndex, degree, degree + 6);
	expected[size_t(pitchClassFromMidi(chordRootMidi))] = true;
	expected[size_t(pitchClassFromMidi(chordRootMidi + i3))] = true;
	expected[size_t(pitchClassFromMidi(chordRootMidi + i5))] = true;
	expected[size_t(pitchClassFromMidi(chordRootMidi + i7))] = true;
	return expected;
}

int scoreMaskMatch(const std::array<bool, 12>& inputMask, const std::array<bool, 12>& expectedMask, int rootPcCandidate) {
	int matches = 0;
	int missing = 0;
	int extras = 0;
	for (int pc = 0; pc < 12; ++pc) {
		const bool inInput = inputMask[size_t(pc)];
		const bool inExpected = expectedMask[size_t(pc)];
		if (inInput && inExpected) matches++;
		else if (!inInput && inExpected) missing++;
		else if (inInput && !inExpected) extras++;
	}

	int score = matches * 10 - missing * 6 - extras * 5;
	if (inputMask[size_t(rootPcCandidate)]) score += 2;
	return score;
}

struct GeneratedChord {
	std::array<int, 4> intervals{{0, 4, 7, 10}};
	std::string suffix = "7";
};

GeneratedChord buildChordForDegree(int scaleIndex, int degree, int typeIndex) {
	GeneratedChord out;
	const int third = intervalFromDegree(scaleIndex, degree, degree + 2);
	const int second = intervalFromDegree(scaleIndex, degree, degree + 1);
	const int fourth = intervalFromDegree(scaleIndex, degree, degree + 3);
	const int fifth = intervalFromDegree(scaleIndex, degree, degree + 4);
	const int seventhDiat = intervalFromDegree(scaleIndex, degree, degree + 6);

	switch (clampInt(typeIndex, 0, int(TYPE_NAMES.size()) - 1)) {
		case 0:
			out.intervals = {{0, third, fifth, seventhDiat}};
			if (third == 4 && fifth == 7) out.suffix = "Maj7";
			else if (third == 3 && fifth == 7) out.suffix = "m7";
			else if (third == 3 && fifth == 6) out.suffix = "m7b5";
			else out.suffix = "Diat7";
			break;
		case 1:
			out.intervals = {{0, second, fifth, seventhDiat}};
			out.suffix = "sus2";
			break;
		case 2:
			out.intervals = {{0, fourth, fifth, seventhDiat}};
			out.suffix = "sus4";
			break;
		case 3:
			out.intervals = {{0, third, fifth, 10}};
			out.suffix = (third <= 3) ? "m7" : "7";
			break;
		case 4:
		default:
			out.intervals = {{0, third, fifth, 9}};
			out.suffix = (third <= 3) ? "m6" : "6";
			break;
	}

	for (int i = 1; i < 4; ++i) {
		while (out.intervals[size_t(i)] <= out.intervals[size_t(i - 1)]) {
			out.intervals[size_t(i)] += 12;
		}
	}
	return out;
}

std::vector<int> buildMidiCandidatesForPitchClass(int pitchClass, int lo, int hi) {
	std::vector<int> out;
	for (int midi = lo; midi <= hi; ++midi) {
		if (pitchClassFromMidi(midi) == pitchClass) {
			out.push_back(midi);
		}
	}
	return out;
}

struct VoicingChoice {
	bool ok = false;
	std::array<int, 4> midi{{60, 64, 67, 71}};
	int span = -1;
	int movement = std::numeric_limits<int>::max();
};

bool isBetterChoice(const VoicingChoice& cand, const VoicingChoice& best) {
	if (!cand.ok) return false;
	if (!best.ok) return true;
	if (cand.span != best.span) return cand.span > best.span;
	if (cand.movement != best.movement) return cand.movement < best.movement;
	const int candCenter = cand.midi[0] + cand.midi[3];
	const int bestCenter = best.midi[0] + best.midi[3];
	return candCenter < bestCenter;
}

VoicingChoice chooseVoicingPass(
		const std::array<int, 4>& tonePitchClasses,
		int lo,
		int hi,
		const std::array<int, 4>& prevVoicing,
		bool havePrev) {
	VoicingChoice best;

	for (const auto& perm : VOICE_PERMUTATIONS) {
		std::array<std::vector<int>, 4> choices;
		bool valid = true;
		for (int v = 0; v < 4; ++v) {
			choices[size_t(v)] = buildMidiCandidatesForPitchClass(tonePitchClasses[size_t(perm[size_t(v)])], lo, hi);
			if (choices[size_t(v)].empty()) {
				valid = false;
				break;
			}
		}
		if (!valid) continue;

		for (int n0 : choices[0]) {
			for (int n1 : choices[1]) {
				if (n1 <= n0) continue;
				for (int n2 : choices[2]) {
					if (n2 <= n1) continue;
					for (int n3 : choices[3]) {
						if (n3 <= n2) continue;

						VoicingChoice cand;
						cand.ok = true;
						cand.midi = {{n0, n1, n2, n3}};
						cand.span = n3 - n0;
						cand.movement = 0;
						if (havePrev) {
							for (int i = 0; i < 4; ++i) {
								cand.movement += std::abs(cand.midi[size_t(i)] - prevVoicing[size_t(i)]);
							}
						}

						if (isBetterChoice(cand, best)) {
							best = cand;
						}
					}
				}
			}
		}
	}

	return best;
}

VoicingChoice chooseBestVoicing(
		const std::array<int, 4>& tonePitchClasses,
		int lo,
		int hi,
		const std::array<int, 4>& prevVoicing,
		bool havePrev) {
	VoicingChoice best = chooseVoicingPass(tonePitchClasses, lo, hi, prevVoicing, havePrev);
	if (best.ok) return best;

	best = chooseVoicingPass(tonePitchClasses, lo - 12, hi + 12, prevVoicing, havePrev);
	if (best.ok) return best;

	return best;
}

std::array<int, 4> fallbackVoicingFromChordRoot(int chordRootMidi, const GeneratedChord& chord, int lo, int hi) {
	std::array<int, 4> out{{
		chordRootMidi + chord.intervals[0],
		chordRootMidi + chord.intervals[1],
		chordRootMidi + chord.intervals[2],
		chordRootMidi + chord.intervals[3]
	}};

	while (out[0] < lo) {
		for (int i = 0; i < 4; ++i) out[size_t(i)] += 12;
	}
	while (out[3] > hi) {
		for (int i = 0; i < 4; ++i) out[size_t(i)] -= 12;
	}
	return out;
}

std::string formatNoteSpread(const std::array<int, 4>& midiNotes) {
	return midiToNoteName(midiNotes[0]) + " " +
		midiToNoteName(midiNotes[1]) + " " +
		midiToNoteName(midiNotes[2]) + " " +
		midiToNoteName(midiNotes[3]);
}

struct DropdownField;

struct DropdownItem : ui::MenuItem {
	DropdownField* owner = nullptr;
	int index = 0;

	void onAction(const event::Action& e) override;
	void step() override;
};

struct DropdownField : OpaqueWidget {
	std::string title;
	int optionCount = 0;
	std::function<int()> getCurrent;
	std::function<void(int)> setCurrent;
	std::function<std::string(int)> getLabel;

	int safeCurrent() const {
		if (!getCurrent) return 0;
		return clampInt(getCurrent(), 0, std::max(0, optionCount - 1));
	}

	std::string safeLabel(int index) const {
		if (!getLabel) return "-";
		return getLabel(clampInt(index, 0, std::max(0, optionCount - 1)));
	}

	void draw(const DrawArgs& args) override {
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, 0.f, 0.f, box.size.x, box.size.y, 2.5f);
		nvgFillColor(args.vg, nvgRGB(0x22, 0x29, 0x34));
		nvgFill(args.vg);

		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, 0.f, 0.f, box.size.x, box.size.y, 2.5f);
		nvgStrokeWidth(args.vg, 1.f);
		nvgStrokeColor(args.vg, nvgRGB(0x7f, 0x8c, 0x9b));
		nvgStroke(args.vg);

		const std::string text = safeLabel(safeCurrent());
		std::shared_ptr<Font> font = APP->window->loadFont(asset::system("res/fonts/DejaVuSans.ttf"));
		if (font) {
			nvgFontFaceId(args.vg, font->handle);
			nvgFontSize(args.vg, 8.0f);
			nvgFillColor(args.vg, nvgRGB(0xec, 0xee, 0xf2));
			nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
			nvgText(args.vg, 4.f, box.size.y * 0.5f, text.c_str(), nullptr);
		}

		nvgBeginPath(args.vg);
		nvgMoveTo(args.vg, box.size.x - 10.f, box.size.y * 0.38f);
		nvgLineTo(args.vg, box.size.x - 6.f, box.size.y * 0.62f);
		nvgLineTo(args.vg, box.size.x - 2.f, box.size.y * 0.38f);
		nvgStrokeWidth(args.vg, 1.2f);
		nvgStrokeColor(args.vg, nvgRGB(0xec, 0xee, 0xf2));
		nvgStroke(args.vg);
	}

	void onButton(const event::Button& e) override {
		if (e.button != GLFW_MOUSE_BUTTON_LEFT || e.action != GLFW_PRESS) {
			OpaqueWidget::onButton(e);
			return;
		}
		e.consume(this);

		ui::Menu* menu = createMenu();
		auto* label = new ui::MenuLabel();
		label->text = title;
		menu->addChild(label);

		for (int i = 0; i < optionCount; ++i) {
			auto* item = new DropdownItem();
			item->owner = this;
			item->index = i;
			item->text = safeLabel(i);
			menu->addChild(item);
		}
	}
};

void DropdownItem::onAction(const event::Action& e) {
	if (owner && owner->setCurrent) {
		owner->setCurrent(index);
	}
}

void DropdownItem::step() {
	rightText = "";
	if (owner && owner->safeCurrent() == index) {
		rightText = "x";
	}
	ui::MenuItem::step();
}

struct ChordTextDisplay : TransparentWidget {
	std::function<std::string()> getText;
	std::shared_ptr<Font> font;

	ChordTextDisplay() {
		font = APP->window->loadFont(asset::system("res/fonts/DejaVuSans.ttf"));
	}

	void draw(const DrawArgs& args) override {
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, 0.f, 0.f, box.size.x, box.size.y, 2.5f);
		nvgFillColor(args.vg, nvgRGB(0x22, 0x29, 0x34));
		nvgFill(args.vg);
		nvgStrokeWidth(args.vg, 1.f);
		nvgStrokeColor(args.vg, nvgRGB(0x7f, 0x8c, 0x9b));
		nvgStroke(args.vg);

		std::string text = "-";
		if (getText) {
			text = getText();
		}

		nvgFontSize(args.vg, 8.2f);
		if (font) {
			nvgFontFaceId(args.vg, font->handle);
		}
		nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
		nvgFillColor(args.vg, nvgRGB(0xec, 0xee, 0xf2));
		nvgText(args.vg, 4.f, box.size.y * 0.5f, text.c_str(), nullptr);
	}
};

struct PanelLabel : TransparentWidget {
	std::string text;
	int fontSize = 9;
	NVGcolor color = nvgRGB(0x0f, 0x17, 0x2a);
	int align = NVG_ALIGN_LEFT | NVG_ALIGN_TOP;

	void draw(const DrawArgs& args) override {
		std::shared_ptr<Font> font = APP->window->loadFont(asset::system("res/fonts/DejaVuSans.ttf"));
		if (!font) return;
		nvgFontFaceId(args.vg, font->handle);
		nvgFontSize(args.vg, float(fontSize));
		nvgFillColor(args.vg, color);
		nvgTextAlign(args.vg, align);
		nvgText(args.vg, 0.f, 0.f, text.c_str(), nullptr);
	}
};

} // namespace

struct ChordGenModule : Module {
	enum ParamIds {
		ON_PARAM,
		SCALE_PARAM,
		ROOT_PARAM,
		RANGE_PARAM,
		OCT_PARAM,
		TYPE_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		CHORD_POLY_INPUT,
		RANGE_CV_INPUT,
		OCT_CV_INPUT,
		TYPE_CV_INPUT,
		TRIG_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		VOICE1_OUTPUT,
		VOICE2_OUTPUT,
		VOICE3_OUTPUT,
		VOICE4_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		ON_LIGHT,
		NUM_LIGHTS
	};

	dsp::ClockDivider uiDivider;
	std::array<int, 4> currentVoicingMidi{{60, 64, 67, 71}};
	std::array<int, 4> prevVoicingMidi{{60, 64, 67, 71}};
	bool haveVoicing = false;
	bool havePrevVoicing = false;
	bool triggerModeEnabled = true;
	dsp::SchmittTrigger onButtonTrigger;
	dsp::SchmittTrigger trigTickTrigger;

	int lastDegree = -999;
	int lastScale = -1;
	int lastRoot = -1;
	int lastRange = -1;
	int lastOct = -1;
	int lastType = -1;

	std::string chordTypeDisplay = "-";
	std::string voicingDisplay = "-";

	ChordGenModule() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);

		configParam(ON_PARAM, 0.f, 1.f, 0.f, "Trigger mode On/Off");
		configParam(SCALE_PARAM, 0.f, float(SCALE_NAMES.size() - 1), 0.f, "Scale");
		configParam(ROOT_PARAM, 0.f, 11.f, 0.f, "Root");
		configParam(RANGE_PARAM, 1.f, 3.f, 2.f, "Range Octaves");
		configParam(OCT_PARAM, 0.f, 4.f, 2.f, "Start Octave");
		configParam(TYPE_PARAM, 0.f, float(TYPE_NAMES.size() - 1), 0.f, "Chord Type");

		paramQuantities[SCALE_PARAM]->snapEnabled = true;
		paramQuantities[ROOT_PARAM]->snapEnabled = true;
		paramQuantities[RANGE_PARAM]->snapEnabled = true;
		paramQuantities[OCT_PARAM]->snapEnabled = true;
		paramQuantities[TYPE_PARAM]->snapEnabled = true;

		configInput(CHORD_POLY_INPUT, "Chord Degree Poly input");
		configInput(RANGE_CV_INPUT, "Range CV");
		configInput(OCT_CV_INPUT, "Start Octave CV");
		configInput(TYPE_CV_INPUT, "Type CV");
		configInput(TRIG_INPUT, "Trigger tick input");
		configOutput(VOICE1_OUTPUT, "Voice 1 mono V/Oct");
		configOutput(VOICE2_OUTPUT, "Voice 2 mono V/Oct");
		configOutput(VOICE3_OUTPUT, "Voice 3 mono V/Oct");
		configOutput(VOICE4_OUTPUT, "Voice 4 mono V/Oct");

		uiDivider.setDivision(64);
	}

	int readScaleIndex() {
		return clampInt(int(std::round(params[SCALE_PARAM].getValue())), 0, int(SCALE_NAMES.size()) - 1);
	}

	int readRootPitchClass() {
		return clampInt(int(std::round(params[ROOT_PARAM].getValue())), 0, 11);
	}

	int readRangeValue() {
		float cvNorm = 0.f;
		if (inputs[RANGE_CV_INPUT].isConnected()) {
			cvNorm = clamp(inputs[RANGE_CV_INPUT].getVoltage() / 10.f, -1.f, 1.f);
		}
		const float raw = params[RANGE_PARAM].getValue() + cvNorm * 2.f;
		return clampInt(int(std::round(raw)), 1, 3);
	}

	int readTypeValue() {
		float cvNorm = 0.f;
		if (inputs[TYPE_CV_INPUT].isConnected()) {
			cvNorm = clamp(inputs[TYPE_CV_INPUT].getVoltage() / 10.f, -1.f, 1.f);
		}
		const float raw = params[TYPE_PARAM].getValue() + cvNorm * 4.f;
		return clampInt(int(std::round(raw)), 0, int(TYPE_NAMES.size()) - 1);
	}

	int readStartOctaveValue() {
		float cvNorm = 0.f;
		if (inputs[OCT_CV_INPUT].isConnected()) {
			cvNorm = clamp(inputs[OCT_CV_INPUT].getVoltage() / 10.f, -1.f, 1.f);
		}
		const float raw = params[OCT_PARAM].getValue() + cvNorm * 4.f;
		return clampInt(int(std::round(raw)), 0, 4);
	}

	int readDegreeFromPolyInput(int scaleIndex, int rootPc) {
		if (!inputs[CHORD_POLY_INPUT].isConnected()) return -1;
		const int channels = clampInt(inputs[CHORD_POLY_INPUT].getChannels(), 0, 16);
		if (channels <= 0) return -1;

		const int used = std::min(channels, 4);
		std::array<bool, 12> inputMask{};
		for (int c = 0; c < used; ++c) {
			const int midi = voltsToMidi(inputs[CHORD_POLY_INPUT].getVoltage(c));
			inputMask[size_t(pitchClassFromMidi(midi))] = true;
		}

		const int degCount = degreeCount(scaleIndex);
		int bestDegree = -1;
		int bestScore = std::numeric_limits<int>::min();
		for (int d = 0; d < degCount; ++d) {
			const auto expected = makeExpectedDiatonicMask(scaleIndex, rootPc, d);
			const int rootPcCand = (rootPc + scaleDegreeToSemitone(scaleIndex, d)) % 12;
			const int score = scoreMaskMatch(inputMask, expected, rootPcCand);
			if (score > bestScore) {
				bestScore = score;
				bestDegree = d;
			}
		}

		if (bestDegree < 0) return -1;
		if (bestScore < 8) return -1;
		return bestDegree;
	}

	void clearOutputs() {
		for (int i = 0; i < 4; ++i) {
			outputs[VOICE1_OUTPUT + i].setChannels(1);
			outputs[VOICE1_OUTPUT + i].setVoltage(0.f);
		}
	}

	void rebuildVoicing(int scaleIndex, int rootPc, int rangeValue, int startOctave, int typeValue, int degree) {
		const GeneratedChord chord = buildChordForDegree(scaleIndex, degree, typeValue);
		const int tonicMidi = startOctave * 12 + rootPc;
		const int chordRootMidi = scaleDegreeToMidi(tonicMidi, scaleIndex, degree);

		std::array<int, 4> tonePcs{};
		for (int i = 0; i < 4; ++i) {
			tonePcs[size_t(i)] = pitchClassFromMidi(chordRootMidi + chord.intervals[size_t(i)]);
		}

		const int targetSpan = rangeValue * 12;
		int lo = clampInt(startOctave * 12, 0, 120 - targetSpan);
		const int hi = lo + targetSpan;

		const VoicingChoice choice = chooseBestVoicing(tonePcs, lo, hi, prevVoicingMidi, havePrevVoicing);
		if (choice.ok) {
			currentVoicingMidi = choice.midi;
		} else {
			currentVoicingMidi = fallbackVoicingFromChordRoot(chordRootMidi, chord, lo, hi);
		}

		haveVoicing = true;
		havePrevVoicing = true;
		prevVoicingMidi = currentVoicingMidi;

		chordTypeDisplay = std::string(noteNameFromPitchClass(pitchClassFromMidi(chordRootMidi))) + chord.suffix;
		voicingDisplay = formatNoteSpread(currentVoicingMidi);
	}

	void process(const ProcessArgs& args) override {
		if (onButtonTrigger.process(params[ON_PARAM].getValue())) {
			triggerModeEnabled = !triggerModeEnabled;
		}
		lights[ON_LIGHT].setBrightness(triggerModeEnabled ? 1.f : 0.f);

		const int scaleIndex = readScaleIndex();
		const int rootPc = readRootPitchClass();
		const int rangeValue = readRangeValue();
		const int startOctave = readStartOctaveValue();
		const int typeValue = readTypeValue();

		auto clearStateAndOutputs = [this]() {
			clearOutputs();
			haveVoicing = false;
			havePrevVoicing = false;
			lastDegree = -999;
			lastOct = -1;
			if (uiDivider.process()) {
				chordTypeDisplay = "-";
				voicingDisplay = "-";
			}
		};

		if (triggerModeEnabled) {
			const bool tick = inputs[TRIG_INPUT].isConnected() && trigTickTrigger.process(inputs[TRIG_INPUT].getVoltage());
			if (tick) {
				const int degree = readDegreeFromPolyInput(scaleIndex, rootPc);
				if (degree < 0) {
					clearStateAndOutputs();
				} else {
					rebuildVoicing(scaleIndex, rootPc, rangeValue, startOctave, typeValue, degree);
					lastDegree = degree;
					lastScale = scaleIndex;
					lastRoot = rootPc;
					lastRange = rangeValue;
					lastOct = startOctave;
					lastType = typeValue;
				}
			}
		} else {
			const int degree = readDegreeFromPolyInput(scaleIndex, rootPc);
			if (degree < 0) {
				clearStateAndOutputs();
				return;
			}

			const bool changed =
				!haveVoicing ||
				degree != lastDegree ||
				scaleIndex != lastScale ||
				rootPc != lastRoot ||
				rangeValue != lastRange ||
				startOctave != lastOct ||
				typeValue != lastType;

			if (changed) {
				rebuildVoicing(scaleIndex, rootPc, rangeValue, startOctave, typeValue, degree);
				lastDegree = degree;
				lastScale = scaleIndex;
				lastRoot = rootPc;
				lastRange = rangeValue;
				lastOct = startOctave;
				lastType = typeValue;
			}
		}

		if (!haveVoicing) {
			clearOutputs();
			return;
		}
		for (int i = 0; i < 4; ++i) {
			outputs[VOICE1_OUTPUT + i].setChannels(1);
			outputs[VOICE1_OUTPUT + i].setVoltage(midiToVolts(currentVoicingMidi[size_t(i)]));
		}
	}
};

struct ChordGenWidget : ModuleWidget {
	ChordGenWidget(ChordGenModule* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/ChordGen.svg")));

		auto addLabel = [this](float xMm,
					float yMm,
					const char* txt,
					int size = 8,
					NVGcolor color = nvgRGB(0x0f, 0x17, 0x2a),
					int align = NVG_ALIGN_LEFT | NVG_ALIGN_TOP) {
			auto* l = createWidget<PanelLabel>(mm2px(Vec(xMm, yMm)));
			l->text = txt;
			l->fontSize = size;
			l->color = color;
			l->align = align;
			addChild(l);
		};

		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2.f * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2.f * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addLabel(6.0f, 6.0f, "CHORD GEN", 11, nvgRGB(0x0b, 0x12, 0x20));
		auto* buildLabel = createWidget<PanelLabel>(mm2px(Vec(57.0f, 6.5f)));
		buildLabel->text = string::f("BUILD %d", kChordGenBuildNumber);
		buildLabel->fontSize = 7;
		buildLabel->align = NVG_ALIGN_RIGHT | NVG_ALIGN_TOP;
		buildLabel->color = nvgRGB(0x33, 0x41, 0x55);
		addChild(buildLabel);

		addLabel(5.0f, 15.0f, "SCALE", 7);
		auto* scaleField = createWidget<DropdownField>(mm2px(Vec(5.0f, 18.0f)));
		scaleField->box.size = mm2px(Vec(25.0f, 8.0f));
		scaleField->title = "Scale";
		scaleField->optionCount = int(SCALE_NAMES.size());
		scaleField->getCurrent = [module]() {
			if (!module) return 0;
			return clampInt(int(std::round(module->params[ChordGenModule::SCALE_PARAM].getValue())), 0, int(SCALE_NAMES.size()) - 1);
		};
		scaleField->setCurrent = [module](int v) {
			if (!module) return;
			module->params[ChordGenModule::SCALE_PARAM].setValue(float(clampInt(v, 0, int(SCALE_NAMES.size()) - 1)));
		};
		scaleField->getLabel = [](int i) {
			return std::string(SCALE_NAMES[size_t(clampInt(i, 0, int(SCALE_NAMES.size()) - 1))]);
		};
		addChild(scaleField);

		addLabel(33.0f, 15.0f, "ROOT", 7);
		auto* rootField = createWidget<DropdownField>(mm2px(Vec(33.0f, 18.0f)));
		rootField->box.size = mm2px(Vec(22.0f, 8.0f));
		rootField->title = "Root";
		rootField->optionCount = int(ROOT_NAMES.size());
		rootField->getCurrent = [module]() {
			if (!module) return 0;
			return clampInt(int(std::round(module->params[ChordGenModule::ROOT_PARAM].getValue())), 0, 11);
		};
		rootField->setCurrent = [module](int v) {
			if (!module) return;
			module->params[ChordGenModule::ROOT_PARAM].setValue(float(clampInt(v, 0, 11)));
		};
		rootField->getLabel = [](int i) {
			return std::string(ROOT_NAMES[size_t(clampInt(i, 0, 11))]);
		};
		addChild(rootField);

		addLabel(25.0f, 60.0f, "ON", 7, nvgRGB(0x0f, 0x17, 0x2a), NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
		addParam(createParamCentered<LEDButton>(mm2px(Vec(25.0f, 68.0f)), module, ChordGenModule::ON_PARAM));
		addChild(createLightCentered<MediumLight<WhiteLight> >(mm2px(Vec(25.0f, 68.0f)), module, ChordGenModule::ON_LIGHT));

		// Grid-aligned placement (10mm rows/columns) for manual layout iteration.
		addLabel(25.0f, 105.0f, "RANGE", 7, nvgRGB(0x0f, 0x17, 0x2a), NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(25.0f, 113.0f)), module, ChordGenModule::RANGE_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(10.0f, 113.0f)), module, ChordGenModule::RANGE_CV_INPUT));
		addLabel(10.0f, 105.0f, "RNG CV", 6, nvgRGB(0x0f, 0x17, 0x2a), NVG_ALIGN_CENTER | NVG_ALIGN_TOP);

		addLabel(25.0f, 90.0f, "TYPE", 7, nvgRGB(0x0f, 0x17, 0x2a), NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(25.0f, 98.0f)), module, ChordGenModule::TYPE_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(10.0f, 98.0f)), module, ChordGenModule::TYPE_CV_INPUT));
		addLabel(10.0f, 90.0f, "TYPE CV", 6, nvgRGB(0x0f, 0x17, 0x2a), NVG_ALIGN_CENTER | NVG_ALIGN_TOP);

		addLabel(25.0f, 75.0f, "OCT", 7, nvgRGB(0x0f, 0x17, 0x2a), NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(25.0f, 83.0f)), module, ChordGenModule::OCT_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(10.0f, 83.0f)), module, ChordGenModule::OCT_CV_INPUT));
		addLabel(10.0f, 75.0f, "OCT CV", 6, nvgRGB(0x0f, 0x17, 0x2a), NVG_ALIGN_CENTER | NVG_ALIGN_TOP);

		addLabel(10.0f, 60.0f, "TRIG IN", 7, nvgRGB(0x0f, 0x17, 0x2a), NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(10.0f, 68.0f)), module, ChordGenModule::TRIG_INPUT));

		addLabel(10.0f, 45.0f, "POLY IN", 7, nvgRGB(0x0f, 0x17, 0x2a), NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(10.0f, 53.0f)), module, ChordGenModule::CHORD_POLY_INPUT));

		addLabel(30.0f, 35.0f, "CHORD", 7, nvgRGB(0x0f, 0x17, 0x2a), NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
		auto* chordDisplay = createWidget<ChordTextDisplay>(mm2px(Vec(20.2f, 38.5f)));
		chordDisplay->box.size = mm2px(Vec(19.6f, 7.0f));
		chordDisplay->getText = [module]() {
			if (!module) return std::string("-");
			return module->chordTypeDisplay;
		};
		addChild(chordDisplay);

		addLabel(30.0f, 48.0f, "NOTES", 7, nvgRGB(0x0f, 0x17, 0x2a), NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
		auto* notesDisplay = createWidget<ChordTextDisplay>(mm2px(Vec(20.2f, 51.5f)));
		notesDisplay->box.size = mm2px(Vec(19.6f, 7.0f));
		notesDisplay->getText = [module]() {
			if (!module) return std::string("-");
			return module->voicingDisplay;
		};
		addChild(notesDisplay);

		addLabel(50.0f, 105.0f, "V1", 7, nvgRGB(0x0f, 0x17, 0x2a), NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(50.0f, 113.0f)), module, ChordGenModule::VOICE1_OUTPUT));
		addLabel(50.0f, 90.0f, "V2", 7, nvgRGB(0x0f, 0x17, 0x2a), NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(50.0f, 98.0f)), module, ChordGenModule::VOICE2_OUTPUT));
		addLabel(50.0f, 75.0f, "V3", 7, nvgRGB(0x0f, 0x17, 0x2a), NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(50.0f, 83.0f)), module, ChordGenModule::VOICE3_OUTPUT));
		addLabel(50.0f, 60.0f, "V4", 7, nvgRGB(0x0f, 0x17, 0x2a), NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(50.0f, 68.0f)), module, ChordGenModule::VOICE4_OUTPUT));
	}
};

Model* modelChordGen = createModel<ChordGenModule, ChordGenWidget>("ChordGen");
