#include "akjoy.h"

/* --help
 */
 
static void akjoy_config_print_help(struct akjoy_config *config) {
  fprintf(stderr,"Usage: %s [OPTIONS] DEVICE\n",config->exename);
  fprintf(stderr,
    "OPTIONS:\n"
    "  --help                     Print this message.\n"
    "  --hw-model=NAME            auto,xbox,xbox360,n30,sn30\n"
    "  --name=STRING              Device name to submit to uinput.\n"
    "  --vid=INTEGER              Vendor ID to uinput. (0 to repeat device)\n"
    "  --pid=INTEGER              Product ID to uinput. (0 to repeat device)\n"
    "  --daemonize=0|1            1 to daemonize, 0 (default) to run in foreground.\n"
    "MAPPING OPTIONS. 'default', 'disable', or:\n"
    "  --dpad=flip                All devices. By default, Y is +up -down.\n"
    "  --thumb=binary             A,B,X,Y. Analogue for original Xbox.\n"
    "  --bw=binary                Black and White. Analogue by default. Xbox only.\n"
    "  --aux=                     Start, Select, Home. (only default or disable).\n"
    "  --trigger=binary           L1,R1,L2,R2. Some are analogue by default.\n"
    "  --lstick=flip              Left thumbstick. +up -down by default.\n"
    "  --rstick=flip              Right thumbstick. +up -down by default.\n"
  );
}

/* Cleanup.
 */

void akjoy_config_cleanup(struct akjoy_config *config) {
  if (config->devpath) free(config->devpath);
  if (config->name) free(config->name);
}

/* Evaluate primitives.
 */
 
int akjoy_model_eval(const char *src,int srcc) {
  if (!src) return -1;
  if (srcc<0) { srcc=0; while (src[srcc]) srcc++; }
  
  if ((srcc==4)&&!memcmp(src,"auto",4)) return AKJOY_MODEL_AUTO;
  if ((srcc==4)&&!memcmp(src,"xbox",4)) return AKJOY_MODEL_XBOX;
  if ((srcc==7)&&!memcmp(src,"xbox360",7)) return AKJOY_MODEL_XBOX360;
  if ((srcc==3)&&!memcmp(src,"n30",3)) return AKJOY_MODEL_N30;
  if ((srcc==4)&&!memcmp(src,"sn30",4)) return AKJOY_MODEL_SN30;
  
  return -1;
}

int akjoy_xform_eval(const char *src,int srcc) {
  if (!src) return -1;
  if (srcc<0) { srcc=0; while (src[srcc]) srcc++; }
  
  if ((srcc==7)&&!memcmp(src,"default",7)) return AKJOY_XFORM_DEFAULT;
  if ((srcc==7)&&!memcmp(src,"disable",7)) return AKJOY_XFORM_DISABLE;
  if ((srcc==4)&&!memcmp(src,"flip",4)) return AKJOY_XFORM_FLIP;
  if ((srcc==6)&&!memcmp(src,"binary",6)) return AKJOY_XFORM_BINARY;
  
  return -1;
}

int akjoy_int_eval(int *v,const char *src,int srcc) {
  if (!src) return -1;
  if (srcc<0) { srcc=0; while (src[srcc]) srcc++; }
  *v=0;
  int base=10,srcp=0;
  if ((srcc>=3)&&(src[0]=='0')&&((src[1]=='x')||(src[1]=='X'))) {
    base=16;
    srcp=2;
  }
  while (srcp<srcc) {
    char digit=src[srcp++];
         if ((digit>='0')&&(digit<='9')) digit=digit-'0';
    else if ((digit>='a')&&(digit<='f')) digit=digit-'a'+10;
    else if ((digit>='A')&&(digit<='F')) digit=digit-'A'+10;
    else return -1;
    if (digit>=base) return -1;
    (*v)*=base;
    (*v)+=digit;
  }
  return 0;
}

/* Set string field.
 */
 
static int akjoy_config_string(char **dst,const char *src,int srcc) {
  if (!src) srcc=0; else if (srcc<0) { srcc=0; while (src[srcc]) srcc++; }
  char *nv=0;
  if (srcc) {
    if (!(nv=malloc(srcc+1))) return -1;
    memcpy(nv,src,srcc);
    nv[srcc]=0;
  }
  if (*dst) free(*dst);
  *dst=nv;
  return 0;
}

/* Receive key=value option.
 * Returns <0,0,>0 same as akjoy_config_argv.
 */
 
static int akjoy_config_kv(struct akjoy_config *config,const char *k,int kc,const char *v,int vc) {
  if (!k) kc=0; else if (kc<0) { kc=0; while (k[kc]) kc++; }
  if (!v) vc=0; else if (vc<0) { vc=0; while (v[vc]) vc++; }
  
  if ((kc==4)&&!memcmp(k,"help",4)) {
    akjoy_config_print_help(config);
    return 0; // terminate
  }
  
    
  if ((kc==8)&&!memcmp(k,"hw-model",8)) {
    int model=akjoy_model_eval(v,vc);
    if (model<0) {
      fprintf(stderr,"%s: Value for hw-model must be one of: auto,xbox,xbox360,n30,sn30\n",config->exename);
      return -1;
    }
    config->hw_model=model;
    return 1;
  }
  
  if ((kc==4)&&!memcmp(k,"name",4)) {
    if (akjoy_config_string(&config->name,v,vc)<0) return -1;
    return 1;
  }
  
  if ((kc==3)&&!memcmp(k,"vid",3)) {
    int n;
    if (akjoy_int_eval(&n,v,vc)<0) return -1;
    if ((n<0)||(n>0xffff)) {
      fprintf(stderr,"%s: Invalid idVendor %d\n",config->exename,n);
      return -1;
    }
    config->vid=n;
    return 1;
  }
  
  if ((kc==3)&&!memcmp(k,"pid",3)) {
    int n;
    if (akjoy_int_eval(&n,v,vc)<0) return -1;
    if ((n<0)||(n>0xffff)) {
      fprintf(stderr,"%s: Invalid idProduct %d\n",config->exename,n);
      return -1;
    }
    config->pid=n;
    return 1;
  }
  
  if ((kc==9)&&!memcmp(k,"daemonize",9)) {
    int n;
    if (akjoy_int_eval(&n,v,vc)<0) return -1;
    config->daemonize=n;
    return 1;
  }
  
  #define XFORM(fld) { \
    int xform=akjoy_xform_eval(v,vc); \
    if (xform<0) { \
      fprintf(stderr,"%s: '%.*s' is not a known transform: default,disable,flip,binary\n",config->exename,vc,v); \
      return -1; \
    } \
    config->fld=xform; \
    return 1; \
  }
    
  if ((kc==4)&&!memcmp(k,"dpad",4)) XFORM(dpad)
  if ((kc==5)&&!memcmp(k,"thumb",5)) XFORM(thumb)
  if ((kc==2)&&!memcmp(k,"bw",2)) XFORM(bw)
  if ((kc==3)&&!memcmp(k,"aux",3)) XFORM(aux)
  if ((kc==7)&&!memcmp(k,"trigger",7)) XFORM(trigger)
  if ((kc==6)&&!memcmp(k,"lstick",6)) XFORM(lstick)
  if ((kc==6)&&!memcmp(k,"rstick",6)) XFORM(rstick)
  
  #undef XFORM
  
  fprintf(stderr,"%s: Unexpected option '%.*s' = '%.*s'\n",config->exename,kc,k,vc,v);
  return -1;
}

/* Validate configuration, fill in defaults if warranted.
 */
 
static int akjoy_config_ready(struct akjoy_config *config) {
  if (!config->devpath) {
    fprintf(stderr,"%s: Device path required.\n",config->exename);
    return -1;
  }
  return 0;
}

/* Receive argv.
 */
 
int akjoy_config_argv(struct akjoy_config *config,int argc,char **argv) {

  int argp=1,err;
  if (argc>=1) config->exename=argv[0];
  else config->exename="akjoy";
  
  while (argp<argc) {
    const char *arg=argv[argp++];
    
    // Empty or null, illegal.
    if (!arg||!arg[0]) goto _unexpected_;
    
    // Positional args. Just one.
    if (arg[0]!='-') {
      if (config->devpath) goto _unexpected_;
      if (akjoy_config_string(&config->devpath,arg,-1)<0) return -1;
      continue;
    }
    
    // Single dash alone, undefined.
    if (!arg[1]) goto _unexpected_;
    
    // Short options.
    if (arg[1]!='-') {
      arg+=1;
      for (;*arg;arg++) {
        if ((err=akjoy_config_kv(config,arg,1,0,0))<=0) return err;
      }
      continue;
    }
    
    // Double dash alone, undefined.
    if (!arg[2]) goto _unexpected_;
    
    // Long option.
    const char *k=arg+2,*v=0;
    int kc=0;
    while (k[kc]&&(k[kc]!='=')) kc++;
    if (k[kc]=='=') v=k+kc+1;
    else if ((argp<argc)&&argv[argp]&&(argv[argp][0]!='-')) v=argv[argp++];
    if ((err=akjoy_config_kv(config,k,kc,v,-1))<=0) return err;
    continue;
    
   _unexpected_:;
    fprintf(stderr,"%s: Unexpected argument '%s'\n",config->exename,arg);
    return -1;
  }
  
  if (akjoy_config_ready(config)<0) return -1;
  return 1;
}
