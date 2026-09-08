#include <filesystem>
#include <iostream>

namespace liquify {
int test_native_window(const std::filesystem::path &output, bool high_density);
}

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "usage: liquify-native-window-test <output-directory>\n";
    return 1;
  }
  const auto output = std::filesystem::absolute(argv[1]);
  std::filesystem::create_directories(output);
  for (const auto high_density : {true, false}) {
    if (liquify::test_native_window(output, high_density) != 0) {
      return 1;
    }
  }
  std::cout << "native-window: PASS geometry, resize, fonts, hit testing, "
               "palette, floating panels\n";
  return 0;
}
