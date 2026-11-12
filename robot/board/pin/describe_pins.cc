#include <iostream>
#include <string>

#include "robot/board/pin/pin_source.h"

int main(int argc, char** argv) {
  using namespace robot::board;
  if (argc != 2) {
    std::cerr << "Usage: describe_pins <profile_id> (catalog version 1; opens no devices)\n";
    for (const auto& profile : PinProfiles()) std::cerr << "  " << profile.id << '\n';
    return 2;
  }
  auto profile = FindPinProfile(argv[1], 1);
  if (!profile.ok()) {
    std::cerr << profile.status() << '\n';
    return 1;
  }
  const auto& p = **profile;
  std::cout << "# " << p.model << "\n\nProfile: " << p.id << "@" << p.version
            << "\n\nSource-documented mappings; physical wiring is unqualified.\n\n"
            << "| Header | Signal | Functions | Linux controller:line | PWM controller:channel | "
               "Nominal mV | Alternatives | "
               "Restrictions |\n"
            << "| --- | --- | --- | --- | --- | --- | --- | --- |\n";
  for (const auto& pin : p.pins) {
    std::string functions;
    for (const auto function : pin.functions) {
      if (!functions.empty()) functions += ", ";
      functions += PinFunctionName(function);
    }
    const auto pwm = pin.pwm ? pin.pwm->controller + ":" + std::to_string(pin.pwm->channel) : "";
    std::cout << "| " << (pin.connector.empty() ? p.connector : pin.connector) << '.'
              << pin.header_pin << " | " << pin.signal << " | " << functions << " | "
              << pin.chip_label << ':' << pin.line_name << " | " << pwm << " | " << pin.voltage_mv
              << " | " << pin.alternatives << " | " << pin.restrictions << " |\n";
  }
  std::cout << "\nSources:\n\n";
  for (const auto& source : p.sources) std::cout << "- " << source << '\n';
}
