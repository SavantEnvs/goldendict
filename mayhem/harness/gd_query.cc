// gd_query — small CLI over GoldenDict's dictionary engine, used by mayhem/test.sh as the
// functional oracle. Built with the project's normal flags (no sanitizers).
//
//   gd_query <format> <word> <dictionary file> [more files...]
//
// Loads the files with <Fmt>::makeDictionaries() into a fresh index directory and prints, for each
// dictionary found: its name, article/word counts, and the HTML article GoldenDict renders for <word>.
#include <QApplication>
#include <QDir>
#include <QTemporaryDir>
#include <QThreadPool>
#include <stdio.h>
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

using std::string;
using std::vector;

struct Init : Dictionary::Initializing {
  void indexingDictionary( string const & ) throw() {}
};

static void quiet( QtMsgType, QMessageLogContext const &, QString const & ) {}

int main( int argc, char ** argv )
{
  if ( argc < 4 )
  {
    fprintf( stderr, "usage: %s <format> <word> <file>...\n", argv[ 0 ] );
    return 2;
  }
  qputenv( "QT_QPA_PLATFORM", "offscreen" );
  if ( qgetenv( "XDG_RUNTIME_DIR" ).isEmpty() )
    qputenv( "XDG_RUNTIME_DIR", QDir::tempPath().toUtf8() );
  if ( qgetenv( "GD_QUERY_VERBOSE" ).isEmpty() )
    qInstallMessageHandler( quiet );
  int qargc = 1;
  QApplication app( qargc, argv );

  QTemporaryDir tmp;
  if ( !tmp.isValid() )
    return 2;
  string idx = string( QFile::encodeName( tmp.path() + "/" ).constData() );
  string fmt = argv[ 1 ];
  QString word = QString::fromUtf8( argv[ 2 ] );
  vector< string > files;
  for ( int i = 3; i < argc; ++i )
    files.push_back( argv[ i ] );

  Init init;
  vector< sptr< Dictionary::Class > > d;
  try
  {
    if ( fmt == "mdx" ) d = Mdx::makeDictionaries( files, idx, init );
    else if ( fmt == "bgl" ) d = Bgl::makeDictionaries( files, idx, init );
    else if ( fmt == "dsl" ) d = Dsl::makeDictionaries( files, idx, init, 0, 256 );
    else if ( fmt == "zim" ) d = Zim::makeDictionaries( files, idx, init, 0 );
    else if ( fmt == "slob" ) d = Slob::makeDictionaries( files, idx, init, 0 );
    else if ( fmt == "stardict" ) d = Stardict::makeDictionaries( files, idx, init, 0 );
    else if ( fmt == "xdxf" ) d = Xdxf::makeDictionaries( files, idx, init );
    else if ( fmt == "aard" ) d = Aard::makeDictionaries( files, idx, init, 0 );
    else if ( fmt == "sdict" ) d = Sdict::makeDictionaries( files, idx, init );
    else if ( fmt == "gls" ) d = Gls::makeDictionaries( files, idx, init );
    else if ( fmt == "dictd" ) d = DictdFiles::makeDictionaries( files, idx, init );
    else if ( fmt == "lsa" ) d = Lsa::makeDictionaries( files, idx, init );
    else if ( fmt == "zips" ) d = ZipSounds::makeDictionaries( files, idx, init );
    else
    {
      fprintf( stderr, "unknown format %s\n", fmt.c_str() );
      return 2;
    }
  }
  catch ( std::exception & e )
  {
    printf( "error: %s\n", e.what() );
    return 1;
  }

  printf( "dictionaries: %u\n", (unsigned) d.size() );
  for ( size_t i = 0; i < d.size(); ++i )
  {
    printf( "name: %s\n", d[ i ]->getName().c_str() );
    printf( "articles: %lu\nwords: %lu\n", d[ i ]->getArticleCount(), d[ i ]->getWordCount() );
    try
    {
      sptr< Dictionary::DataRequest > a = d[ i ]->getArticle( gd::toWString( word ), vector< gd::wstring >() );
      while ( !a->isFinished() )
        QThreadPool::globalInstance()->waitForDone( 10 );
      long n = a->dataSize();
      if ( n > 0 )
      {
        vector< char > buf( n );
        a->getDataSlice( 0, n, &buf[ 0 ] );
        printf( "article:\n%.*s\n", (int) n, &buf[ 0 ] );
      }
      else
        printf( "article: <none>\n" );
    }
    catch ( std::exception & e )
    {
      printf( "error: %s\n", e.what() );
    }
  }
  QThreadPool::globalInstance()->waitForDone();
  return 0;
}
