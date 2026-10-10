// Keep assertions enabled even when the main plugin is built in Release mode.
#ifdef NDEBUG
#error SARIF path regression requires assertions
#endif
#include "../../src/DTTSarif.h"
#include "llvm/Support/raw_ostream.h"

int main(int argc, char **argv) {
  if (argc != 2)
    return 2;
  llvm::outs() << dtt::findRepoRoot(argv[1]);
}
