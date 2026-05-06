//help: Web browser. Renders HTTP and HTTPS pages in a window.
//help: Usage: browser [url]
//help:   Address bar: Ctrl-L to focus, Enter to go.
//help:   Backspace (page focus): back history.
//help:   Arrow keys / mouse wheel: scroll.
//help:   Click link to navigate.

#include "browser/main.cc"
#include "browser/util.cc"
#include "browser/url.cc"
#include "browser/net.cc"
#include "browser/dom.cc"
#include "browser/parser.cc"
#include "browser/layout.cc"
#include "browser/paint.cc"
#include "browser/nav.cc"
#include "browser/input.cc"

void main() { browser_main(); }
