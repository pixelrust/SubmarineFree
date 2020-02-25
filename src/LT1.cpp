//SubTag W20

#include <string.h>
#include "SubmarineFree.hpp"
#include "settings.hpp"

struct BulkParamWidget : widget::OpaqueWidget {
	Module *module;
	int paramId;
	float *value = NULL;
	float minValue = .0f;
	float maxValue = 1.0f;
	float defaultValue = .0f;
	ui::Tooltip* tooltip = NULL;
	std::string description;
	std::string label;
	std::string unit;
	/** Set to 0 for linear, positive for exponential, negative for logarithmic. */
	float displayBase = 0.f;
	float displayMultiplier = 1.f;
	float displayOffset = 0.f;

	void onButton(const event::Button& e) override;
	void onDoubleClick(const event::DoubleClick& e) override;
	void onEnter(const event::Enter& e) override;
	void onLeave(const event::Leave& e) override;

	/** For legacy patch loading */
	void fromJson(json_t* rootJ);
	void createContextMenu();
	void resetAction();
	virtual void reset() {}
	virtual void randomize() {}

	std::function<void(ui::Menu *)> contextMenuCallback;

	std::string getString() {
		std::string s;
		if (!label.empty())
			s += label + ": ";
		s += getDisplayValueString() + unit;
		return s;
	}
	float getDisplayValue() {
		if (!value)
			return .0f;
		float v = *value;
		if (displayBase == 0.f) {
			// Linear
			// v is unchanged
		}
		else if (displayBase < 0.f) {
			// Logarithmic
			v = std::log(v) / std::log(-displayBase);
		}
		else {
			// Exponential
			v = std::pow(displayBase, v);
		}
		return v * displayMultiplier + displayOffset;
	}
	void setDisplayValue(float displayValue) {
		if (!value)
			return;
		float v = displayValue - displayOffset;
		if (displayMultiplier == 0.f)
			v = 0.f;
		else
			v /= displayMultiplier;
		if (displayBase == 0.f) {
			// Linear
			// v is unchanged
		}
		else if (displayBase < 0.f) {
			// Logarithmic
			v = std::pow(-displayBase, v);
		}
		else {
			// Exponential
			v = std::log(v) / std::log(displayBase);
		}
		*value = v;
	}
	std::string getDisplayValueString() {
		return string::f("%.*g", 5, math::normalizeZero(getDisplayValue()));
	}
	void setDisplayValueString(std::string s) {
		float v = 0.f;
		char suffix[2];
		int n = std::sscanf(s.c_str(), "%f%1s", &v, suffix);
		if (n >= 2) {
			// Parse SI prefixes
			switch (suffix[0]) {
				case 'n': v *= 1e-9f; break;
				case 'u': v *= 1e-6f; break;
				case 'm': v *= 1e-3f; break;
				case 'k': v *= 1e3f; break;
				case 'M': v *= 1e6f; break;
				case 'G': v *= 1e9f; break;
				default: break;
			}
		}
		if (n >= 1)
			setDisplayValue(v);
	}
};

namespace {
	void setBulkParamValue(int thisModuleId, int thisParamId, float thisValue) {
		SchemeModuleWidget *smw = dynamic_cast<SchemeModuleWidget *>(APP->scene->rack->getModule(thisModuleId));
		if (smw) {
			float *value = smw->getBulkParam(thisParamId);
			if (value) *value = thisValue;
		}
	}
}

struct BulkParamTooltip : ui::Tooltip {
	BulkParamWidget* bulkParamWidget;

	void step() override {
		if (bulkParamWidget->value) {
			// Quantity string
			text = bulkParamWidget->getString();
			// Param description
			std::string description = bulkParamWidget->description;
			if (!description.empty())
				text += "\n" + description;
		}
		Tooltip::step();
		// Position at bottom-right of parameter
		box.pos = bulkParamWidget->getAbsoluteOffset(bulkParamWidget->box.size).round();
		// Fit inside parent (copied from Tooltip.cpp)
		assert(parent);
		box = box.nudge(parent->box.zeroPos());
	}
};

void BulkParamWidget::onButton(const event::Button& e) {
	OpaqueWidget::onButton(e);

	// Touch parameter
	if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT && (e.mods & RACK_MOD_MASK) == 0) {
		e.consume(this);
	}

	// Right click to open context menu
	if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_RIGHT && (e.mods & RACK_MOD_MASK) == 0) {
		createContextMenu();
		e.consume(this);
	}
}

void BulkParamWidget::onDoubleClick(const event::DoubleClick& e) {
	resetAction();
}

void BulkParamWidget::onEnter(const event::Enter& e) {
	if (settings::paramTooltip && !tooltip && value) {
		BulkParamTooltip* bulkParamTooltip = new BulkParamTooltip;
		bulkParamTooltip->bulkParamWidget = this;
		APP->scene->addChild(bulkParamTooltip);
		tooltip = bulkParamTooltip;
	}
}

void BulkParamWidget::onLeave(const event::Leave& e) {
	if (tooltip) {
		APP->scene->removeChild(tooltip);
		delete tooltip;
		tooltip = NULL;
	}
}

void BulkParamWidget::fromJson(json_t* rootJ) {
	json_t* valueJ = json_object_get(rootJ, "value");
	if (valueJ) {
		if (value)
			*value = json_number_value(valueJ);
	}
}

void BulkParamWidget::createContextMenu() {
	ui::Menu* menu = createMenu();

	MenuLabel* menuLabel = new MenuLabel;
	menuLabel->text = getString();
	menu->addChild(menuLabel);

	EventParamField *paramField = new EventParamField;
	paramField->box.size.x = 100;
	paramField->text = getDisplayValueString();
	paramField->selectAll();
	paramField->changeHandler = [=](std::string text) {
		float oldValue = *value;
		setDisplayValueString(text);
		float newValue = *value;

		if (oldValue != newValue) {
			int thisModuleId = module->id;
			int thisParamId = paramId;
			APP->history->push(new EventWidgetAction(
				"change parameter",
				[=]() {
					setBulkParamValue(thisModuleId, thisParamId, oldValue);
				},
				[=]() {
					setBulkParamValue(thisModuleId, thisParamId, newValue);
				}
			));	
		}
	};
	menu->addChild(paramField);


	EventWidgetMenuItem *resetItem = new EventWidgetMenuItem;
	resetItem->text = "Initialize";
	resetItem->rightText = "Double-click";
	resetItem->clickHandler = [=]() {
		this->resetAction();
	};
	menu->addChild(resetItem);
	if (contextMenuCallback) {
		contextMenuCallback(menu);
	}
}

void BulkParamWidget::resetAction() {
	if (value) {
		float oldValue = *value;
		reset();
		float newValue = *value;
		if (oldValue != newValue) {
			int thisModuleId = module->id;
			int thisParamId = paramId;
			APP->history->push(new EventWidgetAction(
				"reset parameter",
				[=]() {
					setBulkParamValue(thisModuleId, thisParamId, oldValue);
				},
				[=]() {
					setBulkParamValue(thisModuleId, thisParamId, newValue);
				}
			));
		}
	}
}

/** Implements vertical dragging behavior for BulkParamWidgets */
struct BulkKnob : BulkParamWidget {
	/** Multiplier for mouse movement to adjust knob value */
	float speed = 1.0;
	float oldValue = .0f;
	bool smooth = true;
	/** Enable snapping at integer values */
	bool snap = false;
	float snapValue = NAN;
	/** Drag horizontally instead of vertically */
	bool horizontal = false;

	void onHover(const event::Hover& e) override;
	void onButton(const event::Button& e) override;
	void onDragStart(const event::DragStart& e) override;
	void onDragEnd(const event::DragEnd& e) override;
	void onDragMove(const event::DragMove& e) override;
	void reset() override;
	void randomize() override;
};

void BulkKnob::onHover(const event::Hover& e) {
	math::Vec c = box.size.div(2);
	float dist = e.pos.minus(c).norm();
	if (dist <= c.x) {
		BulkParamWidget::onHover(e);
	}
}

void BulkKnob::onButton(const event::Button& e) {
	math::Vec c = box.size.div(2);
	float dist = e.pos.minus(c).norm();
	if (dist <= c.x) {
		BulkParamWidget::onButton(e);
	}
}

void BulkKnob::onDragStart(const event::DragStart& e) {
	if (e.button != GLFW_MOUSE_BUTTON_LEFT)
		return;

	if (value) {
		oldValue = *value;
		if (snap) {
			snapValue = *value;
		}
	}

	APP->window->cursorLock();
}

void BulkKnob::onDragEnd(const event::DragEnd& e) {
	if (e.button != GLFW_MOUSE_BUTTON_LEFT)
		return;

	APP->window->cursorUnlock();

	if (value) {
		float newValue = *value;
		if (oldValue != newValue) {
			int thisModuleId = module->id;
			int thisParamId = paramId;
			int thisOldValue = oldValue;
			APP->history->push(new EventWidgetAction(
				"move knob",
				[=]() {
					setBulkParamValue(thisModuleId, thisParamId, thisOldValue);
				},
				[=]() {
					setBulkParamValue(thisModuleId, thisParamId, newValue);
				}
			));
		}
	}
}

void BulkKnob::onDragMove(const event::DragMove& e) {
	if (e.button != GLFW_MOUSE_BUTTON_LEFT)
		return;

	if (value) {
		float range = 2.0f;
		if (std::isfinite(minValue) && std::isfinite(maxValue)) {
			range = maxValue - minValue;
		}
		float delta = (horizontal ? e.mouseDelta.x : -e.mouseDelta.y);
		delta *= 0.0015f;
		delta *= speed;
		delta *= range;

		// Drag slower if mod is held
		int mods = APP->window->getMods();
		if ((mods & RACK_MOD_MASK) == RACK_MOD_CTRL) {
			delta /= 16.f;
		}
		// Drag even slower if mod+shift is held
		if ((mods & RACK_MOD_MASK) == (RACK_MOD_CTRL | GLFW_MOD_SHIFT)) {
			delta /= 256.f;
		}

		if (snap) {
			snapValue += delta;
			snapValue = math::clamp(snapValue, minValue, maxValue);
			*value = std::round(snapValue);
		}
		else {
			*value += delta;
		}
	}

	BulkParamWidget::onDragMove(e);
}

void BulkKnob::reset() {
	if (value)
		oldValue = snapValue = *value = defaultValue;
}

void BulkKnob::randomize() {
	if (value && std::isfinite(minValue) && std::isfinite(maxValue)) {
		*value = math::rescale(random::uniform(), 0.f, 1.f, minValue, maxValue);
		if (snap)
			*value = std::round(*value);
		oldValue = snapValue = *value;
	}
}

struct BulkLightKnob : BaseLightKnob, BulkKnob {
	BulkLightKnob() {smooth = false;}
	float getBLKValue() override {
		if (value)
			return *value;
		return BaseLightKnob::getBLKValue();
	}
	float getBLKMinValue() override {
		return minValue;
	}
	float getBLKMaxValue() override {
		return maxValue;
	}
	void draw(const DrawArgs &args) override {
		doDraw(args);
	}
};

namespace {
	float clipboard[256];
	bool clipboardUsed = false;
	const int bulkParamSize = sizeof(float) * 256;

	template <class K>
	K* createBulkParamCentered(math::Vec pos, float minValue, float maxValue, float defaultValue, std::string label = "", std::string unit = "", float displayBase = 0.f, float displayMultiplier = 1.f, float displayOffset = 0.f) {
		K* widget = new K();
		widget->box.pos = pos.minus(widget->box.size.div(2));
		widget->minValue = minValue;
		widget->maxValue = maxValue;
		widget->defaultValue = defaultValue;
		widget->label = label;
		widget->unit = unit;
		widget->displayBase = displayBase;
		widget->displayMultiplier = displayMultiplier;
		widget->displayOffset = displayOffset;
		return widget;
	}
	
}

struct LT_116 : Module {
	enum ParamIds {
		PARAM_OUTPUT_CHANNELS,
		NUM_PARAMS
	};
	enum InputIds {
		INPUT_1,
		NUM_INPUTS
	};
	enum OutputIds {
		OUTPUT_1,
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};

	alignas(16) float bulkParams[256] = {.0f};

	int numberOfInputs = 1;
	int numberOfOutputs = 16;
	
	LT_116() : Module() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(PARAM_OUTPUT_CHANNELS, 1.0f, 16.0f, 16.0f, "Number of channels in output");
	}

	json_t *dataToJson() override {
		json_t *rootJ = json_object();
		json_t *arr = json_array();
		for (unsigned int i = 0; i < 256; i++) {
			json_array_append_new(arr, json_real(bulkParams[i]));
		}
		json_object_set_new(rootJ, "coefficients", arr);
		return rootJ;
	}

	void dataFromJson(json_t *rootJ) override {
		json_t *arr = json_object_get(rootJ, "coefficients");
		if (arr) {
			int size = json_array_size(arr);
			if (size > 256)
				size = 256;
			for (int i = 0; i < size; i++) {
				json_t *j1 = json_array_get(arr, i);
				if (j1) {
					bulkParams[i] = json_real_value(j1);
				}
			}
		}
	}

	void process(const ProcessArgs &args) override {
		numberOfInputs = inputs[INPUT_1].getChannels();
		numberOfOutputs = params[PARAM_OUTPUT_CHANNELS].getValue();

		alignas(16) float portValues[16];
		inputs[INPUT_1].readVoltages(portValues);
		
		__m128 a,b,c,d;
		a = b = c = d = _mm_setzero_ps();
		float *index = bulkParams;
		for (int j = 0; j < numberOfInputs; j++) {
			__m128 i = _mm_set_ps1(portValues[j]);
			__m128 x = _mm_load_ps(index);
			a = _mm_add_ps(a, _mm_mul_ps(i,x));
			index += 4;
			x = _mm_load_ps(index);
			b = _mm_add_ps(b, _mm_mul_ps(i,x));
			index += 4;
			x = _mm_load_ps(index);
			c = _mm_add_ps(c, _mm_mul_ps(i,x));
			index += 4;
			x = _mm_load_ps(index);
			d = _mm_add_ps(d, _mm_mul_ps(i,x));
			index += 4; 
		}
		_mm_store_ps(portValues, a);
		_mm_store_ps(portValues + 4, b);
		_mm_store_ps(portValues + 8, c);
		_mm_store_ps(portValues + 12, d);

		outputs[OUTPUT_1].setChannels(numberOfOutputs);
		outputs[OUTPUT_1].writeVoltages(portValues);
	}
};

struct LT116 : SchemeModuleWidget {
	BulkLightKnob *knobs[256];
	float *bulkParams;
	LT116(LT_116 *module) {
		setModule(module);
		if (module)
			bulkParams = module->bulkParams;
		this->box.size = Vec(300, 380);
		addChild(new SchemePanel(this->box.size));

		for (unsigned int i = 0; i < 16; i++) {
			for (unsigned int j = 0; j < 16; j++) {
				int index = i * 16 + j;
				knobs[index] = createBulkParamCentered<TinyKnob<BulkLightKnob>>(Vec(15 + j * 18, 30 + i * 18), -INFINITY, +INFINITY, .0f, string::f("Coefficient [%d,%d]", j + 1, i + 1));
				if (module) {
					knobs[index]->value = &(bulkParams[index]);
					knobs[index]->contextMenuCallback = [=](Menu *menu) {
						this->appendCopyPasteMenu(menu);
					};
					knobs[index]->module = module;
					knobs[index]->paramId = index;
				}
				
				addChild(knobs[index]);
			}
		}
		addInput(createInputCentered<SilverPort>(Vec(35, 330), module, LT_116::INPUT_1));
		addOutput(createOutputCentered<SilverPort>(Vec(265, 330), module, LT_116::OUTPUT_1));
		addParam(createParamCentered<SnapKnob<SmallKnob<LightKnob>>>(Vec(200, 330), module, LT_116::PARAM_OUTPUT_CHANNELS));
	}
	float *getBulkParam(int id) override {
		if (id < 0)
			return NULL;
		if (id > 255)
			return NULL;
		if (module)
			return bulkParams + id;	
		return NULL;
	}
	void appendCopyPasteMenu(Menu *menu) {
		if (!module)
			return;
		menu->addChild(new MenuSeparator);

		EventWidgetMenuItem *cmi = createMenuItem<EventWidgetMenuItem>("Copy");
		cmi->clickHandler = [=]() {
			copy();
		};
		menu->addChild(cmi);
		
		if (clipboardUsed) {
			EventWidgetMenuItem *pmi = createMenuItem<EventWidgetMenuItem>("Paste");
			pmi->clickHandler = [=]() {
				paste();
			};
			menu->addChild(pmi);
		}
	}
	void appendContextMenu(Menu *menu) override {
		appendCopyPasteMenu(menu);
		SchemeModuleWidget::appendContextMenu(menu);
	}
	void step() override {
		if (!module)
			return;
		LT_116 *ltModule = dynamic_cast<LT_116 *>(module);
		for (int i = 0; i < 16; i++) {
			for (int j = 0; j < 16; j++) {
				knobs[i * 16 + j]->setEnabled((j < ltModule->numberOfOutputs) && (i < ltModule->numberOfInputs));
			}
		}
		SchemeModuleWidget::step();
	}
	void render(NVGcontext *vg, SchemeCanvasWidget *canvas) override {
		drawBase(vg, "LT-116");

		// Text
		drawText(vg, 35, 352, NVG_ALIGN_CENTER | NVG_ALIGN_BASELINE, 8, gScheme.getContrast(module), "INPUT");
		drawText(vg, 265, 352, NVG_ALIGN_CENTER | NVG_ALIGN_BASELINE, 8, gScheme.getContrast(module), "OUTPUT");
		drawText(vg, 200, 352, NVG_ALIGN_CENTER | NVG_ALIGN_BASELINE, 8, gScheme.getContrast(module), "CHANNELS");
		drawText(vg, 50, 330, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, 16, gScheme.getContrast(module), "\xE2\x86\x93"); 
		drawText(vg, 240, 330, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, 16, gScheme.getContrast(module), "\xE2\x86\x92"); 
	}
	void bulkChangeWithHistory(std::string label, float *values) {
		float oldValues[256];
		float newValues[256];
		memcpy(oldValues, bulkParams, bulkParamSize);
		memcpy(bulkParams, values, bulkParamSize);
		memcpy(newValues, bulkParams, bulkParamSize);
		int moduleId = module->id;
		APP->history->push(new EventWidgetAction(
			label,
			[=]() {
				LT116 *mw = dynamic_cast<LT116 *>(APP->scene->rack->getModule(moduleId));	
				if (mw) {
					memcpy(mw->bulkParams, oldValues, bulkParamSize);
				}
			},
			[=]() {
				LT116 *mw = dynamic_cast<LT116 *>(APP->scene->rack->getModule(moduleId));	
				if (mw) {
					memcpy(mw->bulkParams, newValues, bulkParamSize);
				}
			}
		));	
	}
	void copy() {
		clipboardUsed = true;
		memcpy(clipboard, bulkParams, bulkParamSize);
	}
	void paste() {
		bulkChangeWithHistory("paste values", clipboard);
	}
};

Model *modelLT116 = createModel<LT_116, LT116>("LT-116");
