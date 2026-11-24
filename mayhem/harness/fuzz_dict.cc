// libFuzzer harness for GoldenDict's dictionary-format loaders.
//
// One source, compiled once per format with -DGD_FMT_<NAME>. Each input is written as a dictionary
// file into a fresh scratch directory (with an empty index directory beside it), handed to that
// format's public loader <Fmt>::makeDictionaries() exactly as loaddictionaries.cc does when the user
// adds a dictionary folder, and every dictionary it returns is then queried the way the UI does:
// headword list, article lookup and resource lookup. The scratch directory is removed after every
// input, so a run never reuses an index built from an earlier input.
//
// Multi-file formats (StarDict .ifo/.idx/.dict, dictd .index/.dict) take the input as sections:
// [u32 little-endian length][bytes] repeated, one per file, in that order.
#include <QApplication>
#include <QDir>
#include <QStringList>
#include <QThreadPool>
#include <QByteArray>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include <exception>

#include "dictionary.hh"
#include "wstring_qt.hh"
#include "aard.hh"
#include "bgl.hh"
#include "dictdfiles.hh"
#include "dsl.hh"
#include "gls.hh"
#include "lsa.hh"
#include "mdx.hh"
#include "sdict.hh"
#include "slob.hh"
#include "stardict.hh"
#include "xdxf.hh"
#include "zim.hh"
#include "zipsounds.hh"

#if defined( GD_FMT_MDX )
#define GD_EXT "mdx"
#elif defined( GD_FMT_BGL )
#define GD_EXT "bgl"
#elif defined( GD_FMT_DSL )
#define GD_EXT "dsl"
#elif defined( GD_FMT_ZIM )
#define GD_EXT "zim"
#elif defined( GD_FMT_SLOB )
#define GD_EXT "slob"
#elif defined( GD_FMT_XDXF )
#define GD_EXT "xdxf"
#elif defined( GD_FMT_AARD )
#define GD_EXT "aar"
#elif defined( GD_FMT_SDICT )
#define GD_EXT "dct"
#elif defined( GD_FMT_GLS )
#define GD_EXT "gls"
#elif defined( GD_FMT_LSA )
#define GD_EXT "lsa"
#elif defined( GD_FMT_ZIPSOUNDS )
#define GD_EXT "zips"
#endif

using std::string;
using std::vector;

namespace {

struct Init : Dictionary::Initializing {
  void indexingDictionary( string const & ) throw() {}
};

QApplication * app;

// GoldenDict logs every parse error through Qt's message handler; drop it so the fuzzer output stays
// readable. Installed once, never changed by an input.
void quiet( QtMsgType, QMessageLogContext const &, QString const & ) {}

bool writeFile( QString const & path, const uint8_t * data, size_t size )
{
  FILE * f = fopen( QFile::encodeName( path ).constData(), "wb" );
  if ( !f )
    return false;
  bool ok = fwrite( data, 1, size, f ) == size;
  return fclose( f ) == 0 && ok;
}

// Splits [u32 len][bytes]... into n parts; a short/missing trailing part is empty.
vector< QByteArray > sections( const uint8_t * data, size_t size, int n )
{
  vector< QByteArray > out;
  for ( int i = 0; i < n; ++i )
  {
    if ( i == n - 1 )
    {
      out.push_back( QByteArray( (const char *) data, (int) size ) );
      break;
    }
    uint32_t len = 0;
    if ( size >= 4 )
    {
      len = data[ 0 ] | ( data[ 1 ] << 8 ) | ( data[ 2 ] << 16 ) | ( (uint32_t) data[ 3 ] << 24 );
      data += 4;
      size -= 4;
    }
    else
      size = 0;
    if ( len > size )
      len = (uint32_t) size;
    out.push_back( QByteArray( (const char *) data, (int) len ) );
    data += len;
    size -= len;
  }
  return out;
}

void waitFor( sptr< Dictionary::Request > const & r )
{
  if ( !r )
    return;
  while ( !r->isFinished() )
    QThreadPool::globalInstance()->waitForDone( 10 );
}

void exercise( vector< sptr< Dictionary::Class > > & dicts )
{
  for ( size_t i = 0; i < dicts.size(); ++i )
  {
    sptr< Dictionary::Class > d = dicts[ i ];
    d->getName();
    d->getArticleCount();
    d->getWordCount();
    d->getDescription();

    QStringList headwords;
    d->getHeadwords( headwords );
    headwords << "a" << "test";
    for ( int h = 0; h < headwords.size() && h < 4; ++h )
    {
      gd::wstring word = gd::toWString( headwords[ h ] );
      try
      {
        sptr< Dictionary::WordSearchRequest > m = d->prefixMatch( word, 10 );
        waitFor( m );
        sptr< Dictionary::DataRequest > a = d->getArticle( word, vector< gd::wstring >() );
        waitFor( a );
        if ( a && a->dataSize() > 0 )
        {
          vector< char > buf( a->dataSize() );
          a->getDataSlice( 0, buf.size(), &buf[ 0 ] );
        }
        sptr< Dictionary::DataRequest > r = d->getResource( string( headwords[ h ].toUtf8().constData() ) );
        waitFor( r );
      }
      catch ( std::exception & )
      {
      }
    }
  }
  QThreadPool::globalInstance()->waitForDone();
}

} // namespace

extern "C" int LLVMFuzzerInitialize( int * argc, char *** argv )
{
  qputenv( "QT_QPA_PLATFORM", "offscreen" );
  if ( qgetenv( "XDG_RUNTIME_DIR" ).isEmpty() )
    qputenv( "XDG_RUNTIME_DIR", QDir::tempPath().toUtf8() );
  qInstallMessageHandler( quiet );
  static int qargc = 1;
  static char arg0[] = "fuzz_dict";
  static char * qargv[] = { arg0, 0 };
  app = new QApplication( qargc, qargv );
  (void) argc;
  (void) argv;
  return 0;
}

extern "C" int LLVMFuzzerTestOneInput( const uint8_t * data, size_t size )
{
  if ( !app )
    LLVMFuzzerInitialize( 0, 0 );

  QByteArray tmpl = QFile::encodeName( QDir::tempPath() + "/gdfuzz-XXXXXX" );
  if ( !mkdtemp( tmpl.data() ) )
    return 0;
  QString dir = QFile::decodeName( tmpl );
  QDir().mkpath( dir + "/idx" );
  string indexDir = string( QFile::encodeName( dir + "/idx/" ).constData() );
  vector< string > files;
  Init init;

  auto add = [ & ]( QString const & name, const uint8_t * p, size_t n ) {
    QString path = dir + "/" + name;
    if ( writeFile( path, p, n ) )
      files.push_back( string( QFile::encodeName( path ).constData() ) );
  };

#if defined( GD_FMT_STARDICT )
  vector< QByteArray > s = sections( data, size, 3 );
  add( "in.ifo", (const uint8_t *) s[ 0 ].constData(), s[ 0 ].size() );
  add( "in.idx", (const uint8_t *) s[ 1 ].constData(), s[ 1 ].size() );
  add( "in.dict", (const uint8_t *) s[ 2 ].constData(), s[ 2 ].size() );
#elif defined( GD_FMT_DICTD )
  vector< QByteArray > s = sections( data, size, 2 );
  add( "in.index", (const uint8_t *) s[ 0 ].constData(), s[ 0 ].size() );
  add( "in.dict", (const uint8_t *) s[ 1 ].constData(), s[ 1 ].size() );
#else
  add( "in." GD_EXT, data, size );
#endif

  vector< sptr< Dictionary::Class > > dicts;
  try
  {
#if defined( GD_FMT_MDX )
    dicts = Mdx::makeDictionaries( files, indexDir, init );
#elif defined( GD_FMT_BGL )
    dicts = Bgl::makeDictionaries( files, indexDir, init );
#elif defined( GD_FMT_DSL )
    dicts = Dsl::makeDictionaries( files, indexDir, init, 0, 256 );
#elif defined( GD_FMT_ZIM )
    dicts = Zim::makeDictionaries( files, indexDir, init, 0 );
#elif defined( GD_FMT_SLOB )
    dicts = Slob::makeDictionaries( files, indexDir, init, 0 );
#elif defined( GD_FMT_STARDICT )
    dicts = Stardict::makeDictionaries( files, indexDir, init, 0 );
#elif defined( GD_FMT_XDXF )
    dicts = Xdxf::makeDictionaries( files, indexDir, init );
#elif defined( GD_FMT_AARD )
    dicts = Aard::makeDictionaries( files, indexDir, init, 0 );
#elif defined( GD_FMT_SDICT )
    dicts = Sdict::makeDictionaries( files, indexDir, init );
#elif defined( GD_FMT_GLS )
    dicts = Gls::makeDictionaries( files, indexDir, init );
#elif defined( GD_FMT_DICTD )
    dicts = DictdFiles::makeDictionaries( files, indexDir, init );
#elif defined( GD_FMT_LSA )
    dicts = Lsa::makeDictionaries( files, indexDir, init );
#elif defined( GD_FMT_ZIPSOUNDS )
    dicts = ZipSounds::makeDictionaries( files, indexDir, init );
#else
#error "define one GD_FMT_<NAME>"
#endif
    exercise( dicts );
  }
  catch ( std::exception & )
  {
  }
  QThreadPool::globalInstance()->waitForDone();
  dicts.clear();

  QDir( dir ).removeRecursively();
  return 0;
}
