#include "ButtonInputFactory.h"
#include "ButtonInputShiftReg5.h"
#include "ButtonInputGpio2.h"
#include "ButtonInputGpio3Remote.h"

namespace {
  ButtonInputShiftReg5 s_shiftReg5;
  ButtonInputGpio2 s_gpio2;
  ButtonInputGpio3Remote s_gpio3remote;
}

namespace ButtonInputFactory {
  IButtonInput* create(const DeviceConfig &cfg) {
    switch ((ButtonVariant)cfg.buttonVariant) {
      case ButtonVariant::SHIFTREG_5BTN:
      case ButtonVariant::SHIFTREG_3BTN:
        return &s_shiftReg5;
      case ButtonVariant::GPIO_2BTN:
        return &s_gpio2;
      case ButtonVariant::GPIO_3BTN_RMT:
        return &s_gpio3remote;
      case ButtonVariant::NONE:
      default:
        return nullptr;
    }
  }
}
