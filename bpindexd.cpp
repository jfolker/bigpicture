#include <iostream>
#include <string>

int main(int argc, char** argv) {
  std::cout << "TODO: implement bpindexd" << std::endl;

  // Temporary kludge to silence -Wunused-parameter
  std::cerr << "args:\n";
  for (int i=0; i<argc; ++i) {
    std::cerr << argv[i] << " ";
  }
  std::cerr << std::endl;
  return 0;
}
