// Deterministic Gopher protocol tests. No network or Reader hardware required.
#define main reread_gopher_entry
#include "main.cpp"
#undef main
#include <cassert>

int main() {
  const QByteArray sample =
    "iA plain heading\tfake\t(NULL)\t0\r\n"
    "1Open directory\t/sub\tsdf.org\t70\r\n"
    "0Read article\t/article\tgopher.floodgap.com\t70\r\n"
    "7Search here\t/query\tsdf.org\t70\r\n"
    "3Server message\t\tfake\t70\r\n"
    "gA GIF\t/image.gif\tsdf.org\t70\r\n"
    ".\r\n";
  const auto items=parseMenu(sample);
  assert(items.size()==6);
  assert(!items[0].selectable);
  assert(items[1].selectable && items[1].address.host=="sdf.org");
  assert(items[2].selectable && items[2].address.type=='0');
  assert(!items[3].selectable && kindName(items[3].address.type).contains("UNAVAILABLE"));
  assert(!items[4].selectable);
  assert(items[5].selectable);
  assert(display(items[1].address)=="gopher://sdf.org/1/sub");
  assert(unstuff("first\r\n..second\r\n.\r\n")=="first\n.second\n");
  const auto malformed=parseMenu("1Bad port\t/x\tsdf.org\tnan\r\n.\r\n");
  assert(malformed.size()==1 && !malformed[0].selectable);
  assert(supported('9') && !supported('h') && !supported('7'));
  return 0;
}
