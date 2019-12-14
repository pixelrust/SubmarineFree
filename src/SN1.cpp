//SubTag W2

#include "SubmarineFree.hpp"
#include <random>
#include <chrono>

namespace {

	static std::default_random_engine generator(std::chrono::system_clock::now().time_since_epoch().count());

	struct Gridcell {
		float llx;// x-coord lower left
		float lrx;// x-coord lower right
		float ulx;// x-coord upper left
		float urx;// x-coord upper right
		float lly;// y-coord lower left
		float lry;// y-coord lower right
		float uly;// y-coord upper left
		float ury;// y-coord upper right
	};

	const unsigned int maxGridWidth = 32;

	float sineLookup[16] = {
		0.195090322016128f,
		0.555570233019602f,
		0.831469612302545f,
		0.980785280403230f,
		0.980785280403230f,
		0.831469612302545f,
		0.555570233019602f,
		0.195090322016128f,
		-0.195090322016128f,
		-0.555570233019602f,
		-0.831469612302545f,
		-0.980785280403230f,
		-0.980785280403230f,
		-0.831469612302545f,
		-0.555570233019602f,
		-0.195090322016128f
	};
}

struct SN_1 : Module {
	enum ParamIds {
		PARAM_FREQ,
		PARAM_LENGTH,
		NUM_PARAMS
	};
	enum InputIds {
		INPUT_FREQ,
		NUM_INPUTS
	};
	enum OutputIds {
		OUTPUT_1,
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};

	alignas(64) Gridcell grid[maxGridWidth];
	__m128i lfsr;
	int effectiveGridWidth = maxGridWidth;
	float x = 0.0f;
	float y = 0.0f;

	SN_1() : Module() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(PARAM_FREQ, -54.0f, +54.0f, 0.0f, "Frequency", " Hz", dsp::FREQ_SEMITONE, dsp::FREQ_C4);
		configParam(PARAM_LENGTH, 2.0f, maxGridWidth, effectiveGridWidth, "Cycle Length");
		initLFSR(lfsr);
		initGridRow();
		shiftGridRows();
		initGridRow();
	}

	inline void initLFSR(__m128i &lfsr) {
		alignas(16) uint16_t seeds[8];
		std::uniform_int_distribution<int> distribution(1, 0xffffu);
		for (unsigned int i = 0; i < 8; i++) {
			seeds[i] = distribution(generator);
		}
		lfsr = _mm_load_si128((__m128i *)(seeds));
	}

	inline void advanceLFSR(__m128i &lfsr) {
		__m128i mask = _mm_set1_epi16(0x0u);
		mask = _mm_cmpgt_epi16(mask, lfsr);
		mask = _mm_and_si128(mask, _mm_set1_epi16(0x100Bu));
		lfsr = _mm_slli_epi16(lfsr, 1);
		lfsr = _mm_xor_si128(lfsr, mask);
	}
	
	inline void shiftGridRows() {
		std::memmove(grid, (uint8_t *)(grid) + 2 * sizeof(float), sizeof(grid) - 2 * sizeof(float));
	}
	
	inline void initGridRow() {
		alignas(16) uint16_t randomValues[maxGridWidth];
		for (unsigned int i = 0; i < maxGridWidth; i += 8) {
			for (unsigned int x = 0; x < 4; x++) {
				advanceLFSR(lfsr);
			}
			_mm_store_si128((__m128i *)(randomValues + i), lfsr);
		}
		for (unsigned int i = 0; i < maxGridWidth; i++) {
			grid[i].ulx = sineLookup[randomValues[i] & 0xf];
			randomValues[i] += 4;
			grid[i].uly = sineLookup[randomValues[i] & 0xf];
		}
		for (unsigned int i = 0; i < maxGridWidth; i++) {
			unsigned int j = (i + 1) % maxGridWidth;
			grid[i].urx = grid[j].ulx;
			grid[i].ury = grid[j].uly;
		}
	}

	void resetY() {
		y = y - (int)y;
		shiftGridRows();
		initGridRow();
	}

	void resetX() {
		int newEffectiveGridWidth = clamp(params[PARAM_LENGTH].getValue(), 2.0f, maxGridWidth);
		if (effectiveGridWidth != newEffectiveGridWidth) {
			grid[effectiveGridWidth].lrx = grid[(effectiveGridWidth + 1) % maxGridWidth].llx;
			grid[effectiveGridWidth].urx = grid[(effectiveGridWidth + 1) % maxGridWidth].ulx;
			grid[effectiveGridWidth].lry = grid[(effectiveGridWidth + 1) % maxGridWidth].lly;
			grid[effectiveGridWidth].ury = grid[(effectiveGridWidth + 1) % maxGridWidth].uly;
			effectiveGridWidth = newEffectiveGridWidth;
			grid[effectiveGridWidth].lrx = grid[0].llx;
			grid[effectiveGridWidth].urx = grid[0].ulx;
			grid[effectiveGridWidth].lry = grid[0].lly;
			grid[effectiveGridWidth].ury = grid[0].uly;
		}
		int intX = (int)x;
		x -= intX;
		x += intX % effectiveGridWidth;
	}

	void process(const ProcessArgs &args) override {
		float baseFreq = 261.626f;
		float freq = baseFreq * powf(2.0f, params[PARAM_FREQ].getValue() * (1.0f / 12.0f) + (inputs[INPUT_FREQ].isConnected()?inputs[INPUT_FREQ].getVoltage():0.0f));
		float deltaTime = freq * args.sampleTime;
		x = x + deltaTime;
		while (x > effectiveGridWidth)
			resetX();
		outputs[OUTPUT_1].setVoltage(x,0);
	}
};

struct SN101 : SchemeModuleWidget {
	LightKnob *lengthKnob;
	SN101(SN_1 *module) : SchemeModuleWidget(module) {
		this->box.size = Vec(30, 380);
		addChild(new SchemePanel(this->box.size));

		addInput(createInputCentered<SilverPort>(Vec(15,31.5), module, SN_1::INPUT_FREQ));
		addParam(createParamCentered<SmallKnob<LightKnob>>(Vec(15, 70), module, SN_1::PARAM_FREQ));
		lengthKnob = createParamCentered<SnapKnob<SmallKnob<LightKnob>>>(Vec(15, 110), module, SN_1::PARAM_LENGTH);
		addParam(lengthKnob);
		addOutput(createOutputCentered<SilverPort>(Vec(15,350), module, SN_1::OUTPUT_1));
	}
	void render(NVGcontext *vg, SchemeCanvasWidget *canvas) override {
		drawBase(vg, "SN-101");
		drawText(vg, 16, 55, NVG_ALIGN_CENTER | NVG_ALIGN_BASELINE, 8, gScheme.getContrast(module), "FREQ.");
	}
	void step() override {
		if (module) {
			lengthKnob->color = (APP->engine->getParam(module, SN_1::PARAM_LENGTH) == dynamic_cast<SN_1 *>(module)->effectiveGridWidth)?SUBLIGHTBLUE:SUBLIGHTRED;
		}	
		SchemeModuleWidget::step();
	}
};

Model *modelSN101 = createModel<SN_1, SN101>("SN-101");
