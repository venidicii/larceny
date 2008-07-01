/* Copyright 1998 Lars T Hansen       -*- indent-tabs-mode: nil -*-
 *
 * $Id$
 *
 * Larceny run-time system -- main file.
 *
 * FIXME: Over-complex.  A gazillion parameters are supported, which 
 * introduces:
 *   - parsing and error checking
 *   - text into the usage message
 *   - value printing if parameter value printing is on
 * Since various GCs have various parameters, factor this cruft out
 * into a spec that can be handled by the GC, via function pointers or
 * whatever.
 */

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include "config.h"
#include "larceny.h"
#include "gc.h"
#include "stats.h"        /* for stats_init() */
#include "gc_t.h"
#include "young_heap_t.h" /* for yh_create_initial_stack() */

opt_t command_line_options;

static void param_error( char *s );
static void invalid( char *s );
static void usage( void );
static void help(int wizardp);
static void parse_options( int argc, char **argv, opt_t *opt );
static int  getsize( char *s, int *p );
static void dump_options( opt_t *o );

static bool quiet = 0;
  /* 'quiet' controls consolemsg() 
     */

static bool annoying = 0;
  /* 'annoying' controls annoying_msg() 
     */

static bool supremely_annoying = 0;
  /* 'supremely_annoying' controls supremely_annoyingmsg()
     */

static void print_banner() {
#ifndef PETIT_LARCENY
  consolemsg( "%s v%d.%d%s (%s, %s:%s:%s)",
              larceny_system_name,
              larceny_major_version, 
              larceny_minor_version,
              larceny_version_qualifier,
              date,
              larceny_gc_technology,
              osname, 
              (globals[ G_CACHE_FLUSH ] ? "split" : "unified") );
#else
  consolemsg( "%s v%d.%d%s (%s:%s)",
              larceny_system_name,
              larceny_major_version, 
              larceny_minor_version,
              larceny_version_qualifier,
              larceny_gc_technology,
              osname );
#endif
}

#if defined PETIT_LARCENY || defined X86_NASM
int larceny_main( int argc, char **os_argv )
#else
int main( int argc, char **os_argv )
#endif
{
  int generations;
  char **argv;

#if defined(DEC_ALPHA_32BIT)
  /* I know this looks weird.  When running Petit Larceny on the Alpha
     in 32-bit mode, pointers are 32-bit, but the interface to main() is
     64-bit (for reasons I do not understand yet).  The following
     seemingly unnecessary loop moves the argument vector into 32-bit
     space.  Presumably there's a better way, as this must be a common
     problem.
     */
  argv = (char**)malloc( sizeof( char* )*(argc+1) );
  for ( i=0 ; i < argc ; i++ ) {
    argv[i] = (char*)malloc( strlen( os_argv[i] )+1 );
    strcpy( argv[i], os_argv[i] );
  }
  argv[i] = 0;
#else
  argv = os_argv;
#endif

  osdep_init();
  cache_setup();

  memset( &command_line_options, 0, sizeof( command_line_options ) );
  command_line_options.maxheaps = MAX_GENERATIONS;
  command_line_options.timerval = 0xFFFFFFFF;
  command_line_options.heapfile = 0;
  command_line_options.enable_breakpoints = 1;
  command_line_options.restv = 0;
  command_line_options.gc_info.ephemeral_info = 0;
  command_line_options.gc_info.use_static_area = 1;
  command_line_options.gc_info.globals = globals;
#if defined( BDW_GC )
  command_line_options.gc_info.is_conservative_system = 1;
#endif
  command_line_options.nobanner = 0;
  command_line_options.unsafe = 0;
  command_line_options.foldcase = 0;
  command_line_options.nofoldcase = 0;
  command_line_options.r5rs = 0;
  command_line_options.err5rs = 0;
  command_line_options.r6rs = 0;
  command_line_options.r6fast = 0;
  command_line_options.r6slow = 0;
  command_line_options.r6pedantic = 0;
  command_line_options.r6less_pedantic = 0;
  command_line_options.r6program = "";
  command_line_options.r6path = "";

  if (larceny_version_qualifier[0] == '.') {
    /* If we our version qualifier starts with a period, then the
     * version prints out as M.NN.XXX (a development version number).
     * On development versions, we always print the banner with
     * information about the build date, host system, and gc
     * technology. */
    print_banner();
    /* since we printed the banner here, there's no reason to print it
     * again below. */
    command_line_options.nobanner = 1;
  }

  /* FIXME: This should all be factored out as osdep_get_program_options()
     or something like that.  That requires factoring out the type of 'o'
     in a header somewhere, and exposing parse_options.
     */
#if defined(MACOS)
  { /* Look for the file "larceny.args" in the application's home directory. */
    int argc, maxargs = 100;
    char *argv[100], buf[256], *p;
    char *args_filename = "larceny.args"; /* fixme: don't hardwire file name */
    FILE *fp;

    argv[0] = "Petit Larceny";            /* fixme: get application name */
    argc = 1;
    if ((fp = fopen( args_filename, "r")) != 0) {
      while (fgets( buf, sizeof(buf), fp ) != 0) {
        p = strtok( buf, " \t\n\r" );
        while (p != 0 && argc < maxargs-1) {
          argv[argc++] = strdup( p );
          p = strtok( 0, " \t\n\r" );
        }
      }
      fclose( fp );
    }
    argv[argc] = 0;
    parse_options( argc, argv, &command_line_options );
  }
#else
  parse_options( argc, argv, &command_line_options );
#endif

  if (!command_line_options.nobanner)
    print_banner();

  osdep_poll_startup_events();

  if (command_line_options.heapfile == 0) {
    char *path = getenv(LARCENY_ROOT);
    size_t path_length = strlen(path);
    size_t base_length = strlen(larceny_heap_name);

    /* This leaks, but only once at startup.  I think it's worth it: */
    command_line_options.heapfile = malloc(path_length + base_length + 2);
    if (command_line_options.heapfile == NULL) {
      panic_exit("Cannot allocate buffer for heapfile name.");
    }

    sprintf(command_line_options.heapfile, "%s/%s", path, larceny_heap_name);
  }

  quiet = command_line_options.quiet;
  annoying = command_line_options.annoying;
  supremely_annoying = command_line_options.supremely_annoying;

  if (annoying || supremely_annoying)
    dump_options( &command_line_options );

  if (command_line_options.flush)
    globals[ G_CACHE_FLUSH ] = 1;
  else if (command_line_options.noflush)
    globals[ G_CACHE_FLUSH ] = 0;

  if (command_line_options.reorganize_and_dump &&
      !command_line_options.gc_info.is_stopcopy_system) {
    command_line_options.gc_info.is_conservative_system = 0;
    command_line_options.gc_info.is_generational_system = 0;
    command_line_options.gc_info.is_stopcopy_system = 1;
    command_line_options.gc_info.use_static_area = 1;
    command_line_options.gc_info.use_non_predictive_collector = 0;
    command_line_options.gc_info.use_incremental_bdw_collector = 0;
    command_line_options.gc_info.sc_info.size_bytes = DEFAULT_STOPCOPY_SIZE;
    command_line_options.gc_info.sc_info.load_factor = DEFAULT_LOAD_FACTOR;
  }

  if (!create_memory_manager( &command_line_options.gc_info, &generations ))
    panic_exit( "Unable to set up the garbage collector." );

  if (!load_heap_image_from_file( command_line_options.heapfile ))
    panic_exit( "Unable to load the heap image." );

  if (command_line_options.reorganize_and_dump) {
    char buf[ FILENAME_MAX ];   /* Standard C */

    sprintf( buf, "%s.split", command_line_options.heapfile );
    if (!reorganize_and_dump_static_heap( buf ))
      panic_exit( "Failed heap reorganization." );
    return 0;
  }

  /* initialize some policy globals */
  globals[ G_BREAKPT_ENABLE ] =
    (command_line_options.enable_breakpoints ? TRUE_CONST : FALSE_CONST);
  globals[ G_SINGLESTEP_ENABLE ] =
    (command_line_options.enable_singlestep ? TRUE_CONST : FALSE_CONST );
  globals[ G_TIMER_ENABLE ] =
    (command_line_options.enable_timer ? TRUE_CONST : FALSE_CONST );
  globals[ G_TIMER ] = 0;
  globals[ G_TIMER2 ] = command_line_options.timerval;
  globals[ G_RESULT ] = fixnum( 0 );  /* No arguments */

  setup_signal_handlers();
  stats_init( the_gc(globals) );
  scheme_init( globals );

  /* The initial stack can't be created when the garbage collector
     is created because stack creation depends on certain data
     structures allocated by scheme_init() in Petit Larceny when
     the system is compiled with USE_GOTOS_LOCALLY.  So we create
     the stack here.
     */
  yh_create_initial_stack( the_gc(globals)->young_area );

  /* Allocate vector of command line arguments and pass it as an
   * argument to the startup procedure.
   */
  { word args[1], res;

    args[0] = allocate_argument_vector( the_gc(globals),
                                        command_line_options.restc,
                                        command_line_options.restv );
    larceny_call( globals[ G_STARTUP ], 1, args, &res );
    consolemsg( "Startup procedure returned with value %08lx", (long)res );
  }

  /* Not usually reached */
  return 0;
}


/***************************************************************************
 *
 * Console messages
 *
 */

int panic_exit( const char *fmt, ... )
{
  static int in_panic = 0;
  va_list args;

  va_start( args, fmt );
  fprintf( stderr, "Larceny Panic: " );
  vfprintf( stderr, fmt, args );
  va_end( args );
  fprintf( stderr, "\n" );

  if (in_panic) abort();
  in_panic = 1;
  exit( 1 );
  /* Never returns. Return type is 'int' to facilitate an idiom. */
  return 0;
}

int panic_abort( const char *fmt, ... )
{
  static int in_panic = 0;
  va_list args;

  va_start( args, fmt );
  fprintf( stderr, "Larceny Panic: " );
  vfprintf( stderr, fmt, args );
  va_end( args );
  fprintf( stderr, "\n" );

  if (in_panic) abort();
  in_panic = 1;
  abort();
  /* Never returns. Return type is 'int' to facilitate an idiom. */
  return 0;
}

void annoyingmsg( const char *fmt, ... )
{
  va_list args;

  if (!annoying) return;

  va_start( args, fmt );
  if (!quiet) {
    vfprintf( stderr, fmt, args );
    fprintf( stderr, "\n" );
    fflush( stderr );
  }
  va_end( args );
}

void supremely_annoyingmsg( const char *fmt, ... )
{
  va_list args;

  if (!supremely_annoying) return;

  va_start( args, fmt );
  if (!quiet) {
    vfprintf( stderr, fmt, args );
    fprintf( stderr, "\n" );
    fflush( stderr );
  }
  va_end( args );
}

void consolemsg( const char *fmt, ... )
{
  va_list args;

  if (quiet) return;

  va_start( args, fmt );
  vfprintf( stdout, fmt, args );
  fprintf( stdout, "\n" );
  va_end( args );
  fflush( stdout );
}

void hardconsolemsg( const char *fmt, ... )
{
  va_list args;

  va_start( args, fmt );
  vfprintf( stderr, fmt, args );
  va_end( args );
  fprintf( stderr, "\n" );
}

/****************************************************************************
 *
 * Command line parsing.
 */

static int hstrcmp( const char *s1, const char* s2 );
static int sizearg( char *str, int *argc, char ***argv, int *var );
static int hsizearg( char *str, int *argc, char ***argv, int *var, int *loc );
static int doublearg( char *str, int *argc, char ***argv, double *var );
static int numbarg( char *str, int *argc, char ***argv, int *var );
static int hnumbarg( char *str, int *argc, char ***argv, int *var, int *loc );
static void compute_np_parameters( opt_t *o, int suggested_size );

static void init_generational( opt_t *o, int areas, char *name )
{
  if (areas < 2)
    invalid( name );

  if (o->gc_info.ephemeral_info != 0) {
    consolemsg( "Error: Number of areas re-specified with '%s'", name );
    consolemsg( "Type \"larceny -help\" for help." );
    exit( 1 );
  }

  o->gc_info.is_generational_system = 1;
  o->gc_info.ephemeral_info = 
    (sc_info_t*)must_malloc( sizeof( sc_info_t )*areas-2 );
  o->gc_info.ephemeral_area_count = areas-2;
}

static void
parse_options( int argc, char **argv, opt_t *o )
{
  int i, loc, prev_size, areas = DEFAULT_AREAS;
#if defined( BDW_GC )
  double load_factor = 0.0;                   /* Ignore it. */
#else
  double load_factor = DEFAULT_LOAD_FACTOR;
#endif
  double expansion = 0.0;                     /* Ignore it. */
  int divisor = 0;                            /* Ignore it. */
  double feeling_lucky = 0.0;                 /* Not lucky at all. */
  double phase_detection = -1.0;              /* No detection. */
  int np_remset_limit = INT_MAX;              /* Infinity, or close enough. */
  int full_frequency = 0;
  double growth_divisor = 1.0;
  double dof_free_before_collection = 1.0;
  double dof_free_before_promotion = 1.0;
  double dof_free_after_collection = 1.0;
  int dynamic_max = 0;
  int dynamic_min = 0;
  int val;

  while (--argc) {
    ++argv;
#if !defined( BDW_GC )
    if (hstrcmp( *argv, "-stopcopy" ) == 0)
      o->gc_info.is_stopcopy_system = 1;
    else if (numbarg( "-areas", &argc, &argv, &areas ))
      init_generational( o, areas, "-areas" );
    else if (hstrcmp( *argv, "-gen" ) == 0)
      init_generational( o, areas, "-gen" );
    else if (hstrcmp( *argv, "-nostatic" ) == 0)
      o->gc_info.use_static_area = 0;
    else if (hstrcmp( *argv, "-nocontract" ) == 0)
      o->gc_info.dont_shrink_heap = 1;
    else if (hsizearg( "-size", &argc, &argv, &val, &loc )) {
      if (loc > 1) o->gc_info.is_generational_system = 1;
      if (loc < 0 || loc > o->maxheaps)
        invalid( "-size" );
      else if (loc > 0)
        o->size[loc-1] = val;
      else 
        for ( i=1 ; i < o->maxheaps ; i++ )
          if (o->size[i-1] == 0) o->size[i-1] = val;
    }
    else if (sizearg( "-rhash", &argc, &argv, (int*)&o->gc_info.rhash ))
      ;
    else if (sizearg( "-ssb", &argc, &argv, (int*)&o->gc_info.ssb ))
      ;
    else 
#endif /* !BDW_GC */
    if (numbarg( "-ticks", &argc, &argv, (int*)&o->timerval ))
      ;
    else if (doublearg( "-load", &argc, &argv, &load_factor )) {
#if defined(BDW_GC)
      if (load_factor < 1.0 && load_factor != 0.0)
        param_error( "Load factor must be at least 1.0" );
#else
      if (load_factor < 2.0)
        param_error( "Load factor must be at least 2.0" );
#endif
    }
#if ROF_COLLECTOR
    else if (hstrcmp( *argv, "-np" ) == 0 || hstrcmp( *argv, "-rof" ) == 0) {
      o->gc_info.is_generational_system = 1;
      o->gc_info.use_non_predictive_collector = 1;
    }
    else if (numbarg( "-steps", &argc, &argv,
                     &o->gc_info.dynamic_np_info.steps )) {
      o->gc_info.is_generational_system = 1;
      o->gc_info.use_non_predictive_collector = 1;
    }
    else if (sizearg( "-stepsize", &argc, &argv,
                     &o->gc_info.dynamic_np_info.stepsize )) {
      o->gc_info.is_generational_system = 1;
      o->gc_info.use_non_predictive_collector = 1;
    }
    else if (doublearg( "-phase-detection", &argc, &argv, &phase_detection ))
      ;
    else if (numbarg( "-np-remset-limit", &argc, &argv, &np_remset_limit )) 
      ;
#endif
#if DOF_COLLECTOR
    else if (numbarg( "-dof", &argc, &argv, 
                      &o->gc_info.dynamic_dof_info.generations )) {
      o->gc_info.is_generational_system = 1;
      o->gc_info.use_dof_collector = 1;
    }
    else if (numbarg( "-dof-fullgc-frequency", &argc, &argv, &full_frequency)){
      if (full_frequency < 0)
        param_error( "Full GC frequency must be nonnegative." );
    }
    else if (doublearg( "-dof-growth-divisor", &argc, &argv, &growth_divisor)){
      if (growth_divisor <= 0.0)
        param_error( "Growth divisor must be positive." );
    }
    else if (doublearg( "-feeling-lucky", &argc, &argv, &feeling_lucky ))
      ;
    else if (doublearg( "-dof-free-before-promotion", &argc, &argv,
                        &dof_free_before_promotion )) {
      if (dof_free_before_promotion <= 0.0 ||
          dof_free_before_promotion > 1.0)
        param_error( "-dof-free-before-promotion out of range: "
                     "must have 0.0 < d <= 1.0. " );
    }
    else if (doublearg( "-dof-free-before-collection", &argc, &argv,
                        &dof_free_before_collection )) {
      if (dof_free_before_collection <= 0.0 ||
          dof_free_before_collection > 1.0)
        param_error( "-dof-free-before-collection out of range: "
                     "must have 0.0 < d <= 1.0." );
    }
    else if (doublearg( "-dof-free-after-collection", &argc, &argv, 
                        &dof_free_after_collection )) {
      if (dof_free_after_collection <= 0.0)
        param_error( "-dof-free-after-collection out of range: "
                     "must have d > 0.0." );
    }
    else if (hstrcmp( *argv, "-dof-no-shadow-remsets" ) == 0)
      o->gc_info.dynamic_dof_info.no_shadow_remsets = TRUE;
    else if (hstrcmp( *argv, "-dof-fullgc-generational" ) == 0)
      o->gc_info.dynamic_dof_info.fullgc_generational = TRUE;
    else if (hstrcmp( *argv, "-dof-fullgc-on-collection" ) == 0)
      o->gc_info.dynamic_dof_info.fullgc_on_collection = TRUE;
    else if (hstrcmp( *argv, "-dof-fullgc-on-promotion" ) == 0)
      o->gc_info.dynamic_dof_info.fullgc_on_promotion = TRUE;
#endif /* DOF_COLLECTOR */
    else if (hstrcmp( *argv, "-nobreak" ) == 0)
      o->enable_breakpoints = 0;
    else if (hstrcmp( *argv, "-step" ) == 0)
      o->enable_singlestep = 1;
    else if (sizearg( "-min", &argc, &argv, &dynamic_min ))
      ;
    else if (sizearg( "-max", &argc, &argv, &dynamic_max ))
      ;
    else if (hstrcmp( *argv, "-help" ) == 0 || strcmp( *argv, "-h" ) == 0)
      help(0);
    else if (hstrcmp( *argv, "-wizard" ) == 0)
      help(1);
    else if (hstrcmp( *argv, "-quiet" ) == 0) 
      o->quiet = 1;
    else if (hstrcmp( *argv, "-annoy-user" ) == 0)
      o->annoying = 1;
    else if (hstrcmp( *argv, "-annoy-user-greatly" ) == 0) {
      o->annoying = 1;
      o->supremely_annoying = 1;
    }
    else if (hstrcmp( *argv, "-flush" ) == 0)
      o->flush = 1;
    else if (hstrcmp( *argv, "-noflush" ) == 0)
      o->noflush = 1;
    else if (hstrcmp( *argv, "-reorganize-and-dump" ) == 0)
      o->reorganize_and_dump = 1;
    else if (hstrcmp( *argv, "-heap" ) == 0) {
      ++argv;
      --argc;
      o->heapfile = *argv;
    }
    else if (hstrcmp( *argv, "-nobanner" ) == 0)
      o->nobanner = 1;
    else if (hstrcmp( *argv, "-foldcase" ) == 0)
      o->foldcase = 1;
    else if (hstrcmp( *argv, "-nofoldcase" ) == 0)
      o->nofoldcase = 1;
    else if (hstrcmp( *argv, "-r5rs" ) == 0) {
      o->r5rs = 1;
      o->foldcase = 1;
    }
    else if (hstrcmp( *argv, "-err5rs" ) == 0)
      o->err5rs = 1;
    else if (hstrcmp( *argv, "-r6rs" ) == 0) {
      o->r6rs = 1;
      o->nobanner = 1;
    }
    else if (hstrcmp( *argv, "-unsafe" ) == 0)
      o->unsafe = 1;
    else if (hstrcmp( *argv, "-fast" ) == 0)
      o->r6fast = 1;
    else if (hstrcmp( *argv, "-slow" ) == 0)
      o->r6slow = 1;
    else if (hstrcmp( *argv, "-pedantic" ) == 0)
      o->r6pedantic = 1;
    else if (hstrcmp( *argv, "-but-not-that-pedantic" ) == 0)
      o->r6less_pedantic = 1;
    else if (hstrcmp( *argv, "-program" ) == 0) {
      ++argv;
      --argc;
      o->r6program = *argv;
    }
    else if (hstrcmp( *argv, "-path" ) == 0) {
      ++argv;
      --argc;
      /* FIXME */
      if (hstrcmp ( o->r6path, "" ) == 0) {
        o->r6path = *argv;
      }
      else {
        param_error ( "Currently, only one path can be specified. " );
      }
    }
    else if (hstrcmp( *argv, "-args" ) == 0 ||
               strcmp( *argv, "--" ) == 0) {
      o->restc = argc-1;
      o->restv = argv+1;
      break;
    }
#if defined(BDW_GC)
    else if (numbarg( "-divisor", &argc, &argv, &divisor )) {
      if (divisor < 1)
        param_error( "Divisor must be at least 1." );
    }
    else if (doublearg( "-expansion", &argc, &argv, &expansion )) {
      if (expansion < 1.0)
        param_error( "Expansion factor must be at least 1.0" );
    }
#endif
    else if (**argv == '-') {
      consolemsg( "Error: Invalid option '%s'", *argv );
      usage();
    }
    else {
      consolemsg( "Error: Deprecated heap file syntax." );
      usage();
    }
  }

  /* Initial validation */

  if (o->foldcase && o->nofoldcase)
    param_error( "Both -foldcase and -nofoldcase selected." );

  if ((o->r5rs && (o->err5rs || o->r6rs)) ||
      (o->err5rs && (o->r5rs || o->r6rs)) ||
      (o->r6rs && (o->r5rs || o->err5rs)))
    param_error( "More than one of -r5rs -err5rs -r6rs selected." );

  if ((o->r6slow || o->r6pedantic) &&
      ((! (o->r6rs)) || (! (o->r6slow)) ||
       (! (o->r6pedantic)) || (o->r6program == 0)))
    param_error( "Missing one of -r6rs -slow -pedantic -program options." );

  if (o->r6less_pedantic && (! (o->r6pedantic)))
    param_error( "Missing -pedantic option." );

  if (o->r6slow && (strcmp (o->r6path, "") != 0))
    param_error( "The -slow and -path options are incompatible." );

  if ((strcmp (o->r6program, "") != 0) && (! (o->r6rs)))
    param_error( "Missing -r6rs option." );

  if (o->gc_info.is_conservative_system &&
      (o->gc_info.is_generational_system || o->gc_info.is_stopcopy_system))
    param_error( "Both precise and conservative gc selected." );
    
  if (o->gc_info.is_generational_system && o->gc_info.is_stopcopy_system)
    param_error( "Both generational and non-generational gc selected." );

  if (!o->gc_info.is_stopcopy_system && !o->gc_info.is_conservative_system
      && o->gc_info.ephemeral_info == 0)
    init_generational( o, areas, "*invalid*" );

  if (dynamic_max && dynamic_min && dynamic_max < dynamic_min)
    param_error( "Expandable MAX is less than expandable MIN." );

  if (dynamic_max || dynamic_min) {
    int n = (o->gc_info.is_generational_system ? areas-1 : 0);

    if (o->size[n] && dynamic_max && o->size[n] > dynamic_max)
      param_error( "Size of expandable area is larger than selected max." );
    if (o->size[n] && dynamic_min && o->size[n] < dynamic_min)
      param_error( "Size of expandable area is smaller than selected min." );
  }

  /* Complete parameter structure by computing the not-specified values. */
  if (o->gc_info.is_generational_system) {
    int n = areas-1;            /* Index of dynamic generation */

    /* Nursery */
    o->gc_info.nursery_info.size_bytes =
      (o->size[0] > 0 ? o->size[0] : DEFAULT_NURSERY_SIZE);

    /* Ephemeral generations */
    prev_size = o->gc_info.nursery_info.size_bytes;

    for ( i = 1 ; i <= areas-2 ; i++ ) {
      if (o->size[i] == 0)
        o->size[i] = prev_size + DEFAULT_EPHEMERAL_INCREMENT;
      o->gc_info.ephemeral_info[i-1].size_bytes = o->size[i];
      prev_size = o->size[i];
    }

    /* Dynamic generation */
    if (o->gc_info.use_dof_collector && 
        o->gc_info.use_non_predictive_collector) {
      consolemsg( "Error: Both nonpredictive (ROF) and DOF gc selected." );
      consolemsg( "Type \"larceny -help\" for help." );
      exit( 1 );
    }
#if ROF_COLLECTOR
    else if (o->gc_info.use_non_predictive_collector) {
      int size;

      o->gc_info.dynamic_np_info.load_factor = load_factor;
      o->gc_info.dynamic_np_info.dynamic_max = dynamic_max;
      o->gc_info.dynamic_np_info.dynamic_min = dynamic_min;
      if (o->size[n] != 0)
        o->gc_info.dynamic_np_info.size_bytes = o->size[n];
      size = prev_size + DEFAULT_DYNAMIC_INCREMENT;
      if (dynamic_min) size = max( dynamic_min, size );
      if (dynamic_max) size = min( dynamic_max, size );
      compute_np_parameters( o, size );
      if (feeling_lucky < 0.0 || feeling_lucky > 1.0)
        param_error( "NP luck parameter (-feeling-lucky) out of range." );
      else
        o->gc_info.dynamic_np_info.luck = feeling_lucky;
      if (phase_detection != -1.0 &&
          (phase_detection < 0.0 || phase_detection > 1.0))
        param_error( "NP phase detection paramater out of range." );
      else
        o->gc_info.dynamic_np_info.phase_detection = phase_detection;
      if (np_remset_limit < 0)
        param_error( "NP remset limit must be nonnegative." );
      else
        o->gc_info.dynamic_np_info.extra_remset_limit = np_remset_limit;
    }
#endif /* ROF_COLLECTOR */
#if DOF_COLLECTOR
    else if (o->gc_info.use_dof_collector) {
      o->gc_info.dynamic_dof_info.load_factor = load_factor;
      o->gc_info.dynamic_dof_info.dynamic_max = dynamic_max;
      o->gc_info.dynamic_dof_info.dynamic_min = dynamic_min;
      o->gc_info.dynamic_dof_info.full_frequency = full_frequency;
      o->gc_info.dynamic_dof_info.growth_divisor = growth_divisor;
      o->gc_info.dynamic_dof_info.free_before_promotion = 
        dof_free_before_promotion;
      o->gc_info.dynamic_dof_info.free_before_collection = 
        dof_free_before_collection;
      o->gc_info.dynamic_dof_info.free_after_collection = 
        dof_free_after_collection;
      if (o->size[n] == 0) {
        int size = prev_size + DEFAULT_DYNAMIC_INCREMENT;
        if (dynamic_min) size = max( dynamic_min, size );
        if (dynamic_max) size = min( dynamic_max, size );
        o->gc_info.dynamic_dof_info.area_size = size;
      }
      else
        o->gc_info.dynamic_dof_info.area_size = o->size[n];
    }
#endif /* DOF_COLLECTOR */
    else {
      o->gc_info.dynamic_sc_info.load_factor = load_factor;
      o->gc_info.dynamic_sc_info.dynamic_max = dynamic_max;
      o->gc_info.dynamic_sc_info.dynamic_min = dynamic_min;
      if (o->size[n] == 0) {
        int size = prev_size + DEFAULT_DYNAMIC_INCREMENT;
        if (dynamic_min) size = max( dynamic_min, size );
        if (dynamic_max) size = min( dynamic_max, size );
        o->gc_info.dynamic_sc_info.size_bytes = size;
      }
      else
        o->gc_info.dynamic_sc_info.size_bytes = o->size[n];
    }
  }
  else if (o->gc_info.is_stopcopy_system) {
    if (o->size[0] == 0) {
      int size = DEFAULT_STOPCOPY_SIZE;

      if (dynamic_min) size = max( dynamic_min, size );
      if (dynamic_max) size = min( dynamic_max, size );
      o->gc_info.sc_info.size_bytes = size;
    }
    else
      o->gc_info.sc_info.size_bytes = o->size[0]; /* Already validated */
    o->gc_info.sc_info.load_factor = load_factor;
    o->gc_info.sc_info.dynamic_min = dynamic_min;
    o->gc_info.sc_info.dynamic_max = dynamic_max;
  }
  else if (o->gc_info.is_conservative_system) {
    if (load_factor > 0.0 && expansion > 0.0)
      param_error( "-load and -expansion are mutually exclusive." );
    o->gc_info.bdw_info.load_factor = load_factor;
    o->gc_info.bdw_info.expansion_factor = expansion;
    o->gc_info.bdw_info.divisor = divisor;
    o->gc_info.bdw_info.dynamic_min = dynamic_min;
    o->gc_info.bdw_info.dynamic_max = dynamic_max;
  }
}

#if ROF_COLLECTOR
/* Note that by design, we do not take the load factor into account. */
static void compute_np_parameters( opt_t *o, int suggested_size )
{
  int steps = o->gc_info.dynamic_np_info.steps;
  int stepsize = o->gc_info.dynamic_np_info.stepsize;
  int size = o->gc_info.dynamic_np_info.size_bytes;

  if (steps == 0 && stepsize == 0) {
    if (size == 0)
      size = suggested_size;
    stepsize = DEFAULT_STEPSIZE;
    steps = ceildiv( size, stepsize );
  }
  else if (steps != 0 && stepsize == 0) {
    if (size == 0) {
      stepsize = DEFAULT_STEPSIZE;
      size = stepsize * steps;
    }
    else
      stepsize = size / steps;
  }
  else if (steps == 0 && stepsize != 0) {
    if (size == 0) {
      steps = DEFAULT_STEPS;
      size = stepsize * steps;
    }
    else
      steps = size / stepsize;
  }
  else 
    size = stepsize * steps;

  o->gc_info.dynamic_np_info.steps = steps;
  o->gc_info.dynamic_np_info.stepsize = stepsize;
  o->gc_info.dynamic_np_info.size_bytes = size;
}
#endif /* ROF_COLLECTOR */

/* Takes a positive integer only, suffixes K and M are accepted. */
static int sizearg( char *str, int *argc, char ***argv, int *loc ) 
{
  if (hstrcmp( **argv, str ) == 0) {
    if (*argc == 1 || !getsize( *(*argv+1), loc ) || *loc <= 0) {
      char buf[ 128 ];
      sprintf( buf, "%s requires a positive integer.", str );
      invalid( buf );
    }
    ++*argv; --*argc;
    return 1;
  }
  else
    return 0;
}

static int 
hsizearg( char *str, int *argc, char ***argv, int *var, int *loc ) 
{
  int l = strlen(str);

  if (strncmp( **argv, str, l ) == 0) {
    if (*(**argv+l) != 0) {
      if (sscanf( **argv+strlen(str), "%d", loc ) != 1) invalid( str );
    }
    else
      *loc = 0;
    if (*argc == 1 || !getsize( *(*argv+1), var )) invalid( str );
    ++*argv; --*argc;
    return 1;
  }
  else
    return 0;
}

static int 
numbarg( char *str, int *argc, char ***argv, int *loc )
{
  if (hstrcmp( **argv, str ) == 0) {
    if (*argc == 1 || sscanf( *(*argv+1), "%d", loc ) != 1 ) invalid( str );
    ++*argv; --*argc;
    return 1;
  }
  else
    return 0;
}

static int 
doublearg( char *str, int *argc, char ***argv, double *loc )
{
  if (hstrcmp( **argv, str ) == 0) {
    if (*argc == 1 || sscanf( *(*argv+1), "%lf", loc ) != 1 ) invalid( str );
    ++*argv; --*argc;
    return 1;
  }
  else
    return 0;
}

static int 
hnumbarg( char *str, int *argc, char ***argv, int *var, int *loc )
{
  int l = strlen(str);

  if (strncmp( **argv, str, l ) == 0) {
    if (*(**argv+l) != 0) {
      if (sscanf( **argv+strlen(str), "%d", loc ) != 1) invalid( str );
    }
    else
      *loc = 0;
    if (*argc == 1 || sscanf( *(*argv+1), "%d", var ) != 1 ) invalid( str );
    ++*argv; --*argc;
    return 1;
  }
  else
    return 0;
}

static int getsize( char *s, int *p )
{
  int r;
  char c, d;

  r = sscanf( s, "%i%c%c", p, &c, &d );
  if (r == 0) return 0;
  if (r == 1) return 1;
  if (r == 3) return 0;
  if (c == 'M' || c == 'm') {
    *p *= 1024*1024;
    return 1;
  }
  if (c == 'K' || c == 'k') {
    *p *= 1024;
    return 1;
  }
  return 0;
}

static int hstrcmp( const char *s1, const char *s2 )
{
    /* Treat --foo as equivalent to -foo; --foo is standard (in other programs) */
    if (s1[0] == '-' && s1[1] == '-')
        return strcmp( s1+1, s2 );
    else
        return strcmp( s1, s2 );
}

static void dump_options( opt_t *o )
{
  int i;

  consolemsg( "" );
  consolemsg( "Command line parameter dump" );
  consolemsg( "---------------------------" );
  consolemsg( "Stepping: %d", o->enable_singlestep );
  consolemsg( "Breakpoints: %d", o->enable_breakpoints );
  consolemsg( "Timer: %d (val=%d)", o->enable_timer, o->timerval );
  consolemsg( "Heap file: %s", o->heapfile );
  consolemsg( "Quiet: %d", o->quiet );
  consolemsg( "Annoying: %d", o->annoying );
  consolemsg( "Supremely annoying: %d", o->supremely_annoying );
  consolemsg( "Flush/noflush: %d/%d", o->flush, o->noflush );
  consolemsg( "Reorganize and dump: %d", o->reorganize_and_dump );
  consolemsg( "" );
  if (o->gc_info.is_conservative_system) {
    consolemsg( "Using conservative garbage collector." );
    consolemsg( "  Incremental: %d", o->gc_info.use_incremental_bdw_collector);
    consolemsg( "  Inverse load factor: %f", o->gc_info.bdw_info.load_factor );
    consolemsg( "  Inverse expansion factor: %f", 
                o->gc_info.bdw_info.expansion_factor );
    consolemsg( "  Divisor: %d", o->gc_info.bdw_info.divisor );
    consolemsg( "  Min size: %d", o->gc_info.bdw_info.dynamic_min );
    consolemsg( "  Max size: %d", o->gc_info.bdw_info.dynamic_max );
  }
  else if (o->gc_info.is_stopcopy_system) {
    consolemsg( "Using stop-and-copy garbage collector." );
    consolemsg( "  Size (bytes): %d", o->gc_info.sc_info.size_bytes );
    consolemsg( "  Inverse load factor: %f", o->gc_info.sc_info.load_factor );
    consolemsg( "  Using static area: %d", o->gc_info.use_static_area );
    consolemsg( "  Min size: %d", o->gc_info.sc_info.dynamic_min );
    consolemsg( "  Max size: %d", o->gc_info.sc_info.dynamic_max );
  }
  else if (o->gc_info.is_generational_system) {
    consolemsg( "Using generational garbage collector." );
    consolemsg( "  Nursery" );
    consolemsg( "    Size (bytes): %d", o->gc_info.nursery_info.size_bytes );
    for ( i=1 ; i<= o->gc_info.ephemeral_area_count ; i++ ) {
      consolemsg( "  Ephemeral area %d", i );
      consolemsg( "    Size (bytes): %d",
                  o->gc_info.ephemeral_info[i-1].size_bytes );
    }
#if ROF_COLLECTOR
    if (o->gc_info.use_non_predictive_collector ) {
      np_info_t *i = &o->gc_info.dynamic_np_info;
      consolemsg( "  Dynamic area (nonpredictive copying)" );
      consolemsg( "    Steps: %d", i->steps );
      consolemsg( "    Step size (bytes): %d", i->stepsize );
      consolemsg( "    Total size (bytes): %d", i->size_bytes );
      consolemsg( "    Inverse load factor: %f", i->load_factor );
      consolemsg( "    Min size: %d", i->dynamic_min );
      consolemsg( "    Max size: %d", i->dynamic_max );
      consolemsg( "    Luck: %f", i->luck );
    }
    else 
#endif
#if DOF_COLLECTOR
    if (o->gc_info.use_dof_collector) {
      dof_info_t *i = &o->gc_info.dynamic_dof_info;
      consolemsg( "  Dynamic area (deferred-older-first copying)" );
      consolemsg( "    Generations: %d", i->generations );
      consolemsg( "    Size (bytes): %d", i->area_size );
      consolemsg( "    Min size: %d", i->dynamic_min );
      consolemsg( "    Max size: %d", i->dynamic_max );
      consolemsg( "    Inverse load factor: %f", i->load_factor );
      consolemsg( "    FullGC frequency: %d", i->full_frequency );
      consolemsg( "    Growth divisor: %f", i->growth_divisor );
      consolemsg( "    Free before promotion (relative to ephemeral size): %f",
                  i->free_before_promotion );
      consolemsg( "    Free before collection (relative to window size): %f",
                  i->free_before_collection );
      consolemsg( "    Free after collection (relative to ephemeral size): %f",
                  i->free_after_collection );
      consolemsg( "    Shadow remsets? %s", 
                  (i->no_shadow_remsets ? "no" : "yes" ));
      consolemsg( "    Generational full GC? %s",
                  (i->fullgc_generational ? "yes" : "no" ));
      consolemsg( "    Full gc policy counts: %s",
                  (i->fullgc_on_promotion ? "promotions" :
                   (i->fullgc_on_collection ? "collections" : 
                    "window resets")));
    }
    else 
#endif
    {
      sc_info_t *i = &o->gc_info.dynamic_sc_info;
      consolemsg( "  Dynamic area (normal copying)" );
      consolemsg( "    Size (bytes): %d", i->size_bytes );
      consolemsg( "    Inverse load factor: %f", i->load_factor );
      consolemsg( "    Min size: %d", i->dynamic_min );
      consolemsg( "    Max size: %d", i->dynamic_max );
    }
    consolemsg( "  Using non-predictive dynamic area: %d",
               o->gc_info.use_non_predictive_collector );
    consolemsg( "  Using static area: %d", o->gc_info.use_static_area );
  }
  else {
    consolemsg( "ERROR: inconsistency: GC type not known." );
    exit( 1 );
  }
  consolemsg( "---------------------------" );
}

static void param_error( char *s )
{
  consolemsg( "Error: %s", s );
  usage();
}

static void invalid( char *s )
{
  consolemsg( "" );
  consolemsg( "Error: Invalid argument to option: %s", s );
  consolemsg( "Type \"larceny -help\" for help." );
  exit( 1 );
}

static void usage( void )
{
  consolemsg( "" );
  consolemsg( "Usage: larceny [ OPTIONS ][-- ARGUMENTS]" );
  consolemsg( "Type \"larceny -help\" for help." );
  exit( 1 );
}


#define STR(x) STR2(x)
#define STR2(x) #x

static char *helptext[] = {
  "  -heap <filename>",
  "     Select the initial heap image.",
  "  -nofoldcase",
  "     Symbols are case-sensitive (the default; #!fold-case overrides).",
  "  -foldcase",
  "     Symbols are case-insensitive (#!no-fold-case overrides).",
  "  -r5rs",
  "     Enter Larceny's traditional read/eval/print loop (the default).",
  "  -err5rs",
  "     Enter an ERR5RS read/eval/print loop.",
  "  -r6rs",
  "     Execute an R6RS-style program in batch mode.",
  "     The following options may also be specified:",
  "       -program <filename>",
  "          Execute the R6RS-style program found in the file.",
  "       -fast",
  "          Execute the R6RS-style program as compiled code (the default).",
  "       -slow",
  "          Execute in Spanky mode; must be accompanied by -pedantic.",
  "       -pedantic",
  "          Execute in Spanky mode; must be accompanied by -slow.",
  "       -but-not-that-pedantic",
  "          Modifies -pedantic, which must also be specified.",
  "  -path <directory>",
  "     Search the directory when using require or import.",
  "     Only one directory can be specified by a -path option.",
  "  -quiet",
  "     Suppress nonessential messages.",
  "  -nobanner",
  "     Suppress runtime startup banner (implied by -r6rs).",
  "  -- <argument> ...",
  "     Tell (command-line-arguments) to return #(<argument> ...)",
  "     This option, if present, must come last.",
  "     In R5RS and ERR5RS modes, Larceny's standard heap interprets",
  "     these command line arguments:",
  "         -e <expr>",
  "           Evaluate <expr> at startup.",
  "         <file>",
  "           Load the specified file (if it exists) at startup.",
  "  -help",
  "     Print this message.",
  "  -wizard",
  "     Print this message as well as help on wizard options.",
  "",
  0 };

static char *wizardhelptext[] = {
  "  (Wizard options below this point.)",
  "  -unsafe",
  "     Crash spectacularly when errors occur (not yet implemented).",
#if !defined(BDW_GC)
  "  -annoy-user",
  "     Print a bunch of annoying debug messages, usually about GC.",
  "  -annoy-user-greatly",
  "     Print a great many very annoying debug messages, usually about GC.",
  "  -stopcopy",
  "     Select the stop-and-copy collector." ,
  "  -gen",
  "     Select generational collection with the standard generational",
  "     collector.  This is the default collector.",
  "  -areas n",
  "     Select generational collection, with n heap areas.  The default" ,
  "     number of heap areas is "
        STR(DEFAULT_AREAS) 
        ".",
#if ROF_COLLECTOR
  "  -np",
  "  -rof",
  "     Select generational collection with the renewal-oldest-first",
  "     dynamic area (radioactive decay non-predictive collection).",
#endif
#if DOF_COLLECTOR
  "  -dof n",
  "     Select generational collection with the deferred-oldest-first",
  "     dynamic area, using n chunks.",
#endif
  "  -size# nnnn",
  "     Heap area number '#' is given size 'nnnn' bytes.",
  "     This selects generational collection if # > 1.",
#endif
  "  -min nnnn",
  "     Set the lower limit on the size of the expandable (\"dynamic\") area.",
  "  -max nnnn",
  "     Set the upper limit on the size of the expandable (\"dynamic\") area.",
  "  -load d",
  "     Use inverse load factor d to control allocation and collection.",
  "     After a major garbage collection, the collector will resize the heap",
  "     and attempt to keep memory consumption below d*live, where live data",
  "     is computed (sometimes estimated) following the major collection.",
#if !defined(BDW_GC)
  "     In a copying collector, d must be at least 2.0; the default value",
  "     is 3.0.",
#else
  "     In the conservative collector, d must be at least 1.0; no default is",
  "     set, as the conservative collector by default manages heap growth",
  "     using the heap space divisor (see -divisor parameter).  The -load",
  "     and -expansion parameters are mutually exclusive.",
#endif
#if defined(BDW_GC)
  "  -divisor n",
  "     The divisor controls collection frequency: at least heapsize/n bytes",
  "     are allocated between one collection and the next. The default value",
  "     is 4, and n=1 effectively disables GC.  The divisor is the",
  "     conservative collector's default allocation and expansion control.",
  "  -expansion d",
  "     Use expansion factor d to control heap expansion.  Following garbage",
  "     collection the heap size is set to max(live*d,heapsize).  No",
  "     expansion factor is set by default, as the conservative collector",
  "     manages heap growth using the heap space divisor (see -divisor",
  "     parameter).  The -expansion and -load parameters are mutually",
  "     exclusive.",
#endif
#if !defined(BDW_GC)
  "  -nostatic",
  "     Do not use the static area, but load the heap image into the",
  "     garbage-collected heap." ,
  "  -nocontract",
  "     Do not contract the dynamic area according to the load factor, but",
  "     always use all the memory that has been allocated.",
#if ROF_COLLECTOR
  "  -steps n",
  "     Select the initial number of steps in the non-predictive collector.",
  "     This selects generational collection and the non-predictive GC.",
  "  -stepsize nnnn",
  "     Select the size of each step in the non-predictive collector.",
  "     This selects generational collection and the non-predictive GC.",
  "  -phase-detection d",
  "     A fudge factor for the non-predictive collector, 0.0 <= d <= 1.0.",
  "     If the non-predictive remembered set has grown by a factor of more ",
  "     than d for some (short) time, and then has grown by a factor of ",
  "     less than d for the same amount of time, then the collector is ",
  "     allowed to decide that a growth phase has ended and that any data",
  "     in the non-predictive young area may be shuffled into the old area",
  "     by adjusting j.  This parameter is sometimes very effective at",
  "     reducing float.  It does not select anything else, not even the",
  "     nonpredictive GC.  By default, phase detection is off.",
  "     Time is measured in the number of promotions into the young area.",
  "  -feeling-lucky d",
  "     A fudge factor for the non-predictive collector, 0.0 <= d <= 1.0.",
  "     After a non-predictive collection and selection of j and k, d*j steps",
  "     are added to k to adjust for the fact that the collector probably",
  "     over-estimates the amount of live storage.  This probably only makes",
  "     sense in a fixed-heap setting, where under-estimation may cause",
  "     failure, so you have to ask yourself, do I feel lucky?  Well, do you?",
  "     The default value is 0.0.  This does not select anything else, not",
  "     even the nonpredictive GC.",
  "  -np-remset-limit n",
  "     A fudge factor for the non-predictive collector, n >= 0.",
  "     If, after a promotion into the non-predictive young area, the number",
  "     of entries in the remembered set that tracks pointers from the",
  "     non-predictive young area to the non-predictive old area, ",
  "     extrapolated to the point when the young area is full, exceeds n, ",
  "     then the collector is allowed to shuffle the entire contents of the",
  "     young area to the old area and to clear the remembered set.  By",
  "     default, the limit is infinity.  This parameter does not select",
  "     anything else, not even the nonpredictive GC.",
#endif
#if DOF_COLLECTOR
  "  -dof-fullgc-frequency n",
  "     The frequency of policy-triggered full garbage collections in ",
  "     the DOF collector, in terms of the number of collection window",
  "     resets since the last full collection.  Full GC occurs after the",
  "     window reset, so a value of `1' means `every reset'.",
  "     The default value is 0 (never), which is the right thing right now.",
  "  -dof-fullgc-on-collection",
  "     The DOF collector should count DOF collections, rather than window",
  "     resets, when deciding when to trigger the full collector.",
  "  -dof-fullgc-on-promotion",
  "     The DOF collector should count promotions into the DOF area, rather",
  "     than window resets or DOF collections, when deciding when to trigger",
  "     the full collector.  (Only useful for certain experimental work.)",
  "  -dof-fullgc-generational",
  "     Use generational techniques to attempt to speed up the full",
  "     collector.  This may reduce the effectiveness of the collector,",
  "     since some garbage cells are likely to be considered to be live.",
  "  -dof-growth-divisor d",
  "     The speed with which the heap is expanded in the DOF collector.",
  "     The default value is 1.0, which lets the heap grow exactly as",
  "     determined by the computed live size.  Larger numbers slow growth.",
  "  -dof-free-before-promotion d",
  "     A fudge factor for the DOF collector, 0.0 < d <= 1.0.  Before",
  "     a promotion into the DOF area, a full collection will be triggered",
  "     if the amount of free memory is less than d times the size of",
  "     the ephemeral area.  The default value is 1.0, which ensures that",
  "     a promotion can never fail, but it is probably pessimistic:",
  "     survival rates out of the ephemeral generations are usually much",
  "     lower, and choosing a lower value may reduce the frequency of DOF",
  "     collections.",
  "  -dof-free-before-collection d",
  "     A fudge factor for the DOF collector, 0.0 < d <= 1.0.  Before",
  "     a DOF collector, a full collection will be triggered if the",
  "     amount of free memory is less than d times the size of the DOF",
  "     collection window.  The default value is 1.0, which ensures that",
  "     a collection can never fail, but it is probably pessimistic:",
  "     survival rates out of the DOF window are usually much lower, and",
  "     choosing a lower value may reduce the frequency of DOF collections.",
  "  -dof-free-after-collection d",
  "     A fudge factor for the DOF collector, d > 0.0.  After a DOF ",
  "     collection, another collection is triggered unless the amount ",
  "     of free memory is at least d times the size of the ephemeral",
  "     area.  Values larger than 1.0 may be helpful in increasing the",
  "     collector's robustness when running in fixed memory and in reducing",
  "     the number of full collections triggered as a result of too little",
  "     memory being available before GC.  Values of d smaller than 1.0",
  "     are probably dangerous.  The default value is 1.0.",
#endif
  "  -rhash nnnn",
  "     Set the remembered-set hash table size, in elements.  The size must",
  "     be a power of 2.",
  "  -ssb nnnn",
  "     Set the remembered-set Sequential Store Buffer (SSB) size, in "
        "elements.",
#endif
  "  -ticks nnnn",
  "     Set the initial countdown timer interval value.",
  "  -nobreak",
  "     Disable breakpoints." ,
  "  -step",
  "     Enable MAL-level single-stepping." ,
#if !defined(BDW_GC)
  "  -reorganize-and-dump",
  "     Split a heap image into text and data, and save the split heap in a",
  "     file. If Larceny is started with foo.heap, this command will create",
  "     foo.heap.split.  The heap image is not executed.",
#endif
  "" ,
  "Values can be decimal, octal (0nnn), hex (0xnnn), or suffixed",
  "with K (for KB) or M (for MB) when that makes sense." ,
  "",
  0 };

static void help(int wizardp)
{
  int i;

  consolemsg("Usage: larceny [options][-- arg-to-scheme ...]");
  consolemsg("" );
  consolemsg("Options:" );
  for (i=0 ; helptext[i] != 0 ; i++ )
    consolemsg( helptext[i] );
  if (wizardp) {
      for (i=0 ; wizardhelptext[i] != 0 ; i++ )
          consolemsg( wizardhelptext[i] );
  }
  consolemsg("The Larceny User's Manual is available on the web at");
  consolemsg("  http://larceny.ccs.neu.edu/doc/");
  exit( 0 );
}

int memfail( int code, char *fmt, ... )
{
  va_list args;
  static char *code_str[] = { "malloc", "heap", "realloc", "calloc", "rts" };

  va_start( args, fmt );
  fprintf( stderr, "Allocation failed (code '%s').\n", code_str[code] );
  vfprintf( stderr, fmt, args );
  va_end( args );
  fprintf( stderr, "\n" );
  exit( 1 );
  /* Never returns; return type is `int' to facilitate an idiom. */
  return 0;
}

void conditional_abort( void )
{
  char buf[ 10 ];

  while (1) {
    hardconsolemsg( "Abort (yes/no)?" );
    if (fgets( buf, 10, stdin ) == NULL) {
      hardconsolemsg( "EOF -- exiting." );
      exit(1);
    }
    if (strncasecmp( buf, "yes", 3 ) == 0) abort();
    if (strncasecmp( buf, "no", 2 ) == 0) return;
  }
}


/* eof */
