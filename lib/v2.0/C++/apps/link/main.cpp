// SPDX-License-Identifier: GPL-2.0-or-later
#include "LinkAgent.h"
#include "SocatSession.h"
#include <clocale>
#include <iostream>

int main(int argc, char **argv)
{
  std::setlocale(LC_NUMERIC, "C");
  auto options = link_app::Options::parse(argc, argv);
  if (!options) {
    std::cerr << argv[0] << ": " << options.error() << '\n' << link_app::Options::usage();
    return 2;
  }
  if (options->help) { std::cout << link_app::Options::usage(); return 0; }
  try {
    if (options->socatAction)
      return link_app::manageSocatSession(*options->socatAction, argv[0]);
    options->validate();
    link_app::LinkAgent agent(std::move(*options));
    agent.run();
    return 0;
  } catch (const std::exception &error) {
    std::cerr << argv[0] << ": " << error.what() << '\n'
              << "Run '" << argv[0] << " --help' for usage information.\n";
    return 1;
  }
}
