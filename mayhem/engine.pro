# Dictionary-engine subset of goldendict.pro, built as a static library for the fuzz harnesses.
# The full app needs Qt WebKit, which Debian trixie (the base image) no longer ships; the
# dictionary-format parsers do not use it.
TEMPLATE = lib
CONFIG += staticlib exceptions rtti stl c++11
TARGET = gdengine
QT += core gui widgets xml network svg sql
GD = $$PWD/..
INCLUDEPATH += $$GD
DEPENDPATH += $$GD
DEFINES += MAKE_ZIM_SUPPORT NO_EPWING_SUPPORT PROGRAM_VERSION=\\\"mayhem\\\"
OBJECTS_DIR = obj
UI_DIR = ui
MOC_DIR = moc
SRCS = dictionary btreeidx chunkedstorage file folding utf8 wstring wstring_qt gddebug langcoder \
       fsencoding audiolink mdictparser filetype ftshelpers htmlescape mdx bgl bgl_babylon dsl \
       dsl_details zim slob stardict xdxf xdxf2html aard sdict gls dictdfiles lsa zipsounds zipfile \
       indexedzip decompress ripemd iconv mutex config splitfile ufile atomic_rename wildcard \
       language country tiff
for(s, SRCS) {
  SOURCES += $$GD/$${s}.cc
  exists($$GD/$${s}.hh): HEADERS += $$GD/$${s}.hh
}
SOURCES += $$GD/dictzip.c
HEADERS += $$GD/fulltextsearch.hh
FORMS += $$GD/fulltextsearch.ui
