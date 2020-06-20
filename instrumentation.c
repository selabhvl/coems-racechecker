#define _POSIX_C_SOURCE 199309L

#include <assert.h>
#include <limits.h>
#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <stdarg.h>
#include <pthread.h>
#include "instrumentation.h"

#ifdef HAVE_COEMS

extern void itm_init();
extern void cedar_msg(unsigned char adr,unsigned int value);

#define fire_epu_event(epu_id,command_id,data) cedar_msg( epu_id, (data<<16) | (0x0000ffff & command_id));


#endif

#ifdef WANT_IO
# define PRINTF(f_, ...) fprintf((f_), __VA_ARGS__)
static FILE* logfile = NULL;
#else
# define PRINTF(f_, ...) {};
#endif

static int is_first = 1;
static uint64_t (*get_timestamp_ptr)(void);

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

static unsigned long lockcnt = 1;

#ifdef HAVE_COEMS
static unsigned int threadid_epu;
static unsigned int threadid_command;
static unsigned int readaddr_epu;
static unsigned int readaddr_command;
static unsigned int writeaddr_epu;
static unsigned int writeaddr_command;
static unsigned int mutexlockaddr_epu;
static unsigned int mutexlockaddr_command;
static unsigned int mutexunlockaddr_epu;
static unsigned int mutexunlockaddr_command;
static unsigned int line_epu;
static unsigned int line_command;
static unsigned int dyn_base_epu;
static unsigned int dyn_base_command;
static unsigned int pcreate_epu;
static unsigned int pcreate_command;

#define FIRE_mutexlockaddr(data) fire_epu_event(mutexlockaddr_epu,mutexlockaddr_command,data)
#define FIRE_mutexunlockaddr(data) fire_epu_event(mutexunlockaddr_epu,mutexunlockaddr_command,data)
#define FIRE_line(data) fire_epu_event(line_epu,line_command,data)
#define FIRE_writeaddr(data) fire_epu_event(writeaddr_epu,writeaddr_command,data)
#define FIRE_threadid(data) fire_epu_event(threadid_epu,threadid_command,data)
#define FIRE_readaddr(data) fire_epu_event(readaddr_epu,readaddr_command,data)
#define FIRE_pcreate() fire_epu_event(pcreate_epu,pcreate_command,1)

#endif /* HAVE_COEMS */

#define NANOS_PER_SEC 1000000000

#if _POSIX_TIMERS > 0 && defined(_POSIX_MONOTONIC_CLOCK)
  #include <time.h>

    static uint64_t get_timestamp() {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return ts.tv_sec * NANOS_PER_SEC + ts.tv_nsec;
    }

    static uint64_t get_normalized_timestamp(){
        static struct timespec offset;
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        if (is_first) {
            offset = ts;
            is_first = 0;
        }
        return (ts.tv_sec - offset.tv_sec) * NANOS_PER_SEC + ts.tv_nsec - offset.tv_nsec + 1;
    }
#elif defined(__APPLE__)
  #include <mach/mach_time.h>

  static mach_timebase_info_data_t info;
  static void __attribute__((constructor)) init_info() {
    mach_timebase_info(&info);
  }

  static uint64_t get_timestamp() {
    return mach_absolute_time();
  }

  static uint64_t get_normalized_timestamp() {
    static uint64_t offset;
    uint64_t result = mach_absolute_time();
    if (is_first) {
      offset = result - 1;
      is_first = 0;
    }
    return result - offset;
  }
#endif

int parse_hex(const char* in) {
  char buf[5];
  buf[0]=in[0];
  buf[1]=in[1];
  buf[2]=in[2];
  buf[3]=in[3];
  buf[4]='\0';
  return (int)strtol(buf,NULL,16);
}

void _instrumentation_init(int doNormalize) {
#ifdef HAVE_COEMS
  itm_init();
#endif
#ifdef WANT_IO
  const char* fs = getenv("COEMSTRACE");
  if (fs == NULL) {
    fs = "traces.log";
  }
  logfile = fopen(fs, "w");
  if (logfile == NULL) {
    PRINTF(stderr, "What ho! Can't write trace to file: %s.\n",fs);
    exit(1);
  }
#endif

#ifdef HAVE_COEMS
  /* parse EPU parameters */
  const char* s = getenv("COEMSEPUS");
  if (s == NULL) {
    PRINTF(stderr, "No COEMSEPUS configured.\n");
    exit(1);
  }
  if (strlen(s) != 64) {
    PRINTF(stderr, "Wrong number of COEMSEPUS configured.\n");
    exit(1);
  }

  /* TODO: assert correct value range; check that order is stable */
  /*
$ grep ISM epu/spec.epu
//ISM mutexlockaddr: Events[Int]    ->     0  6
////ISM mutexunlockaddr: Events[Int]    ->     0  8
////ISM         line: Events[Int]    ->     8  5
////ISM    writeaddr: Events[Int]    ->     0  4
////ISM     threadid: Events[Int]    ->     2  9
////ISM     readaddr: Events[Int]    ->     0  2
            dyn_base
            pcreateid: Events[Int]
//
  */
  mutexlockaddr_epu = parse_hex(s);
  mutexlockaddr_command = parse_hex(s+4);
  mutexunlockaddr_epu = parse_hex(s+8);
  mutexunlockaddr_command = parse_hex(s+12);
  line_epu = parse_hex(s+16);
  line_command = parse_hex(s+20);
  writeaddr_epu = parse_hex(s+24);
  writeaddr_command = parse_hex(s+28);
  threadid_epu = parse_hex(s+32);
  threadid_command = parse_hex(s+36);
  readaddr_epu = parse_hex(s+40);
  readaddr_command = parse_hex(s+44);
  dyn_base_epu = parse_hex(s+48);
  dyn_base_command = parse_hex(s+52);
  pcreate_epu = parse_hex(s+56);
  pcreate_command = parse_hex(s+60);
  
#endif /* HAVE_COEMS */

  get_timestamp_ptr = &get_timestamp;
  if (doNormalize) {
    get_timestamp_ptr = &get_normalized_timestamp;
  }
#ifdef WANT_IO
  /* Virtual lock: */
  uint64_t ts = (*get_timestamp_ptr)();
  PRINTF(logfile,
          "%" PRIu64 ": threadid = %" PRIu64 "\n"
          "%" PRIu64 ": mutexlockaddr = 1\n",
          ts, (uint64_t) pthread_self(), ts);
#endif
}

void emit_dynamic_addr_event(const uintptr_t base) {
#ifdef WANT_IO
    uint64_t ts = (*get_timestamp_ptr)();
    PRINTF(logfile,
            "%" PRIu64 ": instruction = \"%s\"\n%" PRIu64 ": function = \"%s\"\n"
            "%" PRIu64 ": line = %d\n%" PRIu64 ": column = %d\n"
            "%" PRIu64 ": threadid = %" PRIu64 "\n"
            "%" PRIu64 ": dyn_base = %" PRIuPTR "\n"
          , ts, "_dyn", ts, "<unknw>", ts, 0 /* line */, ts, 0 /* column */
          , ts, (uint64_t) pthread_self()
          , ts, base
	);
#endif
#ifdef HAVE_COEMS
    fire_epu_event(dyn_base_epu, dyn_base_command, (uint16_t)base);
#endif
}

void _instrumentation_log_instr(const char* instr_name, char* function_name, int line, int column,...) {
    pthread_mutex_lock(&lock);
    va_list args;
    va_start(args, column);
#ifdef WANT_IO
    uint64_t ts = (*get_timestamp_ptr)();
#endif
    PRINTF(logfile,
            "%" PRIu64 ": instruction = \"%s\"\n%" PRIu64 ": function = \"%s\"\n"
            "%" PRIu64 ": line = %d\n%" PRIu64 ": column = %d\n"
            "%" PRIu64 ": threadid = %" PRIu64 "\n"
            , ts, instr_name,ts, function_name, ts, line, ts, column, ts, (uint64_t) pthread_self());
#ifdef HAVE_COEMS
    pthread_t me = pthread_self();
    uint32_t me32 = (uint32_t) me;
    uint16_t me16 = ((uint16_t) me32 ) ^ ((uint16_t) (me32 >> 16));
    FIRE_threadid(me16);
    FIRE_line((uint16_t) line);
#endif
    enum type currentType;
    enum type context;
    int haveContext = 0;
    int counter = 0; /* TODO: What does this count?! */
    char* lastFun = NULL; /* As we have to iterate through the varargs, we keep in here the significant ID for debugging etc. */
    while ((currentType = va_arg(args,enum type)) != END) {
        /* Keep "top-most" event around until we're finished handling args. */
        if (!haveContext) { context = currentType; haveContext = 1;}
        switch (currentType) {
            case PT:
            case FUNCTIONCALL: {
	        assert(lastFun == NULL); /* We don't really expect to see this TWICE in one log-call */
                lastFun = va_arg(args, char*);
		char *s = lastFun;
                PRINTF(logfile, "%" PRIu64 ": functioncall = \"%s\"\n", ts,  s);
                if (currentType == FUNCTIONCALL) {
                     break;
                }
                enum pt_call pt_id = va_arg(args, enum pt_call);

                switch (pt_id) {
                case PT_LOCK: {
                    char* nnm = va_arg(args, char*); /* stringLit */
                    PRINTF(logfile, "%" PRIu64 ": mutexlock = \"%s\"\n", ts,  nnm);
                    uintptr_t p = (uintptr_t) va_arg(args, void*);
                    PRINTF(logfile, "%" PRIu64 ": mutexlockaddr = %" PRIuPTR "\n", ts, p);
#ifdef HAVE_COEMS
    	            FIRE_mutexlockaddr((uint16_t) p);
#endif
                    break;
                }
                case PT_UNLOCK: {
                    char* nnm = va_arg(args, char*);
                    PRINTF(logfile, "%" PRIu64 ": mutexunlock = \"%s\"\n", ts,  nnm);
                    uintptr_t p = (uintptr_t) va_arg(args, void*);
		    PRINTF(logfile, "%" PRIu64 ": mutexunlockaddr = %" PRIuPTR "\n", ts, p);
#ifdef HAVE_COEMS
    	            FIRE_mutexunlockaddr((uint16_t) p);
#endif
                    break;
                }
                case PT_CREATE: {
                    char* nnm = va_arg(args, char*);
                    PRINTF(logfile, "%" PRIu64 ": pcreate = \"%s\"\n", ts, nnm);
		    /* For EPUs, we don't print the thread-id yet...pthread_t is platform specific... */
		    PRINTF(logfile, "%" PRIu64 ": pcreateid = 1\n", ts);
#ifdef HAVE_COEMS
    	            FIRE_pcreate();
#endif
                    lockcnt=lockcnt+2; /* odd steps */
                    // XXX Doesn't make sense yet, should be virtual lock, but needs to be for
                    // the CHILD thread, not parent.
		    // PRINTF(logfile, "%" PRIu64 ": mutexlockaddr = %"PRIuPTR"\n", ts, lockcnt);
					
                    //va_arg(args, int);
		    //uintptr_t p = (uintptr_t) va_arg(args, void*);
		    //PRINTF(logfile, "%" PRIu64 ": createpthreadid = %" PRIuPTR "\n", ts, p);
                    break;
                }
                case PT_INIT: {
		  /* Check, see notes in full instrumentation -- disable? */

                    char* nnm = va_arg(args, char*);
		    uintptr_t p = (uintptr_t) va_arg(args, void*);
                    PRINTF(logfile, "%" PRIu64 ": mutexlockini = \"%s\"\n", ts,  nnm);
		    //uintptr_t * pr2 = (uintptr_t *)p;
		    //PRINTF(logfile, "%" PRIu64 ": mutexid = %" PRIuPTR "\n", ts, *pr2);
		    PRINTF(logfile, "%" PRIu64 ": mutexlockid = %" PRIuPTR "\n", ts, p);
                    break;
                }
                case PT_JOIN: {
		  pthread_t p = (pthread_t) va_arg(args, void*);
		  PRINTF(logfile, "%" PRIu64 ": joinid = %" PRIuPTR "\n", ts, (uintptr_t) p);
                  break;
                }
                default: assert(0); /* Unreached */
                } // switch
                counter--;
                break;
            }
            case STRING: {
                va_arg(args, char*);
		/* We don't trust s for now, crashed. TODO: Find out why/who. */
		assert(lastFun != NULL); /* Not sure if anyone else uses this, in which case we'd have an inconsistent lastFun */
                PRINTF(stderr, "%s: unknown call parameter.\n", lastFun);
                break; 
            }  
            case LOCALVAR: {
              enum var_access var_rw = va_arg(args, enum var_access);
	      // start by ld 17-10-11, 17-10-24,18-02-16
	      // var name 
                char* s = va_arg(args, char*);   	
		char* sz = va_arg(args, char*);

		//var type and val
                enum type ccType = va_arg(args,enum type);
                uintptr_t p = (uintptr_t) va_arg(args, void*);
		int varoffset = 0;
		
		// start global var handling 
		enum type gvTypest = va_arg(args,enum type);
		int gvnum = va_arg(args, int);
		
		if (gvTypest == GLOBALVAR && gvnum > 0){
		  int ii;
		  for (ii=0;ii<gvnum;ii++){						
		    // ref global var name
		    va_arg(args, int);	
		    char* gvnm = va_arg(args, char*);
		    
		    // ref var offset
		    va_arg(args, int);	
		    int offset =  va_arg(args, int); 
		    
		    // ref var type and val (addr)
		    //enum type gvvType = va_arg(args,enum type);
		    va_arg(args,enum type);
		    uintptr_t gvval = (uintptr_t) va_arg(args, void*);	
		    
		    if (p == gvval + offset){
		      s = gvnm;
		      varoffset = offset;							
		      //PRINTF(logfile, "%" PRIu64 ": refvarname = %s : %s : %s \n", ts, "we go here tooooo",gvnm,s );
		    } else {
		      s = gvnm;
		      varoffset = -1;
		    }
/* 						PRINTF(logfile, "%" PRIu64 ": refvarname = \"%s\"\n", ts, gvnm);						
						PRINTF(logfile, "%" PRIu64 ": refvaroffset = \"%d\"\n", ts, offset);						
						if (gvvType == POINTEROF) {                        
							uintptr_t * gvval2 = (uintptr_t *)gvval;
							PRINTF(logfile, "%" PRIu64 ": refvaraddr = %" PRIuPTR "\n", ts, *gvval2); 
						} else {
							PRINTF(logfile, "%" PRIu64 ": refvaraddr = %" PRIuPTR "\n", ts, gvval);   
						}  */						
		  }
		}
		// end  global var handling 
				
		if (var_rw == VA_R){
		  PRINTF(logfile, "%" PRIu64 ": readvar = \"%s\"\n", ts, s);
		  PRINTF(logfile, "%" PRIu64 ": readaddr = %" PRIuPTR" \n", ts, p);
#ifdef HAVE_COEMS
    	          FIRE_readaddr((uint16_t) p);
#endif
		} else {
                  assert(var_rw == VA_W);
		  PRINTF(logfile, "%" PRIu64 ": writevar = \"%s\"\n", ts, s);
		  PRINTF(logfile, "%" PRIu64 ": writeaddr = %" PRIuPTR" \n", ts, p);
#ifdef HAVE_COEMS
    	          FIRE_writeaddr((uint16_t) p);
#endif
		}	
		
		if (ccType == POINTEROF) {                        
		  uintptr_t * pr2 = (uintptr_t *)p;
		  PRINTF(logfile, "%" PRIu64 ": varaddr = %" PRIuPTR "\n", ts, *pr2);
		  /* TODO: Let's see where that shows up... */
		  PRINTF(logfile, "%" PRIu64 ": indaddr = %" PRIuPTR "\n", ts, *pr2);
		} else {
		  PRINTF(logfile, "%" PRIu64 ": varaddr = %" PRIuPTR "\n", ts, p);   
		}  
		
		PRINTF(logfile, "%" PRIu64 ": varsize = %s\n", ts, sz);
		PRINTF(logfile, "%" PRIu64 ": varoffset = %d\n", ts, varoffset); 
				
                counter--;
                break;
                // end by ld 17-10-11, 17-10-24
            }		
/*             case GLOBALVAR: {
                // start by ld 17-10-11, 17-10-24,18-02-16
				
				// ref var name				
                char* s = va_arg(args, char*);  
				
				// ref var offset
				va_arg(args, int);	
 				char* s2 = va_arg(args, char*);   
				
				// ref var type and val (addr)
                enum type ccType = va_arg(args,enum type);
                uintptr_t p = (uintptr_t) va_arg(args, void*);	
				
				PRINTF(logfile, "%" PRIu64 ": refvarname = \"%s\"\n", ts, s);					
								
				if (ccType == POINTEROF) {                        
					uintptr_t * pr2 = (uintptr_t *)p;
					PRINTF(logfile, "%" PRIu64 ": refvaraddr = %" PRIuPTR "\n", ts, *pr2); 
				} else {
					PRINTF(logfile, "%" PRIu64 ": refvaraddr = %" PRIuPTR "\n", ts, p);   
				}  
				
				PRINTF(logfile, "%" PRIu64 ": refvaroffset = \"%s\"\n", ts, s2); 
				
                counter--;

                break;
                
                // end by ld 17-10-11, 17-10-24,18-2-17
                
            } */     
            default: {
	      PRINTF(stderr, "Fumbling event of type %d while handling %s/%d.\n", currentType, lastFun, context);
	      /* That was the tag, now gobble the data as well: */
	      va_arg(args, char*);
	      // assert(0); /* Shouldn't really happen. */
            }
        }
        counter++;
    }
    va_end(args);
    pthread_mutex_unlock(&lock);
}

void _instrumentation_flush() {
#ifdef WANT_IO
    fflush(logfile);
#endif
}

void _instrumentation_close() {
#ifdef WANT_IO
    fclose(logfile);
#endif
}
