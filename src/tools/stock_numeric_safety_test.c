/* Stock-file numeric regressions. Run with ASan, UBSan and
 * -fsanitize=float-cast-overflow: the latter is not implied by GCC UBSan. */
#include "../engine/stock.h"
#include "../engine/look.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
static void check(int ok,const char *name)
{
    checks++;
    if(!ok) { fprintf(stderr,"FAIL: %s\n",name); exit(1); }
}

static void reject_value(const char *key,const char *value)
{
    char text[8192];
    se_stock stock;
    int n=snprintf(text,sizeof text,"NAME BAD\n%s %s\n",key,value);
    check(n>0 && (size_t)n<sizeof text,"bounded test fixture");
    check(se_stock_parse(text,(size_t)n,&stock)!=SE_OK,key);
    /* Rejection must also leave defaults, rather than a partially populated
     * structure with a dangerous value for a caller that mishandles rc. */
    check(!stock.name[0] && stock.version==0 && stock.exposure==0,
          "rejected stock resets to safe defaults");
}

static void bake_valid(const char *text,size_t len)
{
    se_stock stock;
    se_look look;
    check(se_stock_parse(text,len,&stock)==SE_OK,"legitimate stock parses");
    check(isfinite(stock.exposure) && isfinite(stock.grain) && isfinite(stock.sat),
          "parsed stock controls remain finite");
    se_look_from_stock(&stock,2,&look);
    check(look.sat>=0 && look.sat<=1024 && look.hal_radius>=1 && look.hal_radius<=48,
          "stock bakes through bounded fixed-point conversions");
}

int main(int argc,char **argv)
{
    static const char *keys[]={"VERSION","EXPOSURE","GRAIN","GRAIN_SIZE","SAT",
        "HUE_SAT","HUE_ROT","HUE_LUM","SPLIT_SH","SPLIT_HI","HAL_TINT","HALATION"};
    static const char *bad[]={"1000001","-1000001","99999999999999999999999999999999999999999999999999999999999999999999",
        "NaN","nan","INF","-inf","1e999","1.2.3","+."};
    char long_number[4096],text[8192];
    size_t i,j;
    for(i=0;i<sizeof keys/sizeof keys[0];i++)
        for(j=0;j<sizeof bad/sizeof bad[0];j++) reject_value(keys[i],bad[j]);
    reject_value("EXPOSURE","33");
    reject_value("EXPOSURE","-33");
    reject_value("[RAW]\nEXPOSURE","33");
    reject_value("[RAW]\nGRAIN","9999999999999999999999999999999999999999");
    reject_value("CURVE","0,0 255,99999999999999999999999999999999999999");
    reject_value("HALATION","50 200 99999999999999999999999999999999999999");
    reject_value("HUE_ROT","0,0,0,0,0,99999999999999999999999999999999999999");
    reject_value("VERSION","NaN\nGRAIN 10");
    memset(long_number,'9',sizeof long_number-1); long_number[sizeof long_number-1]=0;
    reject_value("VERSION",long_number);
    /* Length alone is not an error: thousands of leading zeros still
     * describe an ordinary, finite version number. */
    memset(long_number,'0',sizeof long_number-2);
    long_number[sizeof long_number-2]='3'; long_number[sizeof long_number-1]=0;
    snprintf(text,sizeof text,"NAME LONG_ZERO\nVERSION %s\nGRAIN 12\n",long_number);
    bake_valid(text,strlen(text));
    {
        const char *valid="NAME EDGE\nVERSION 1000000\nEXPOSURE 32\n"
            "GRAIN 1000000\nGRAIN_SIZE 1000000\nSAT 1000000\n"
            "HUE_SAT 1000000,-1000000\nHUE_ROT 1000000,-1000000\n"
            "HUE_LUM 1000000\nSPLIT_SH -1000000,1000000,0\n"
            "SPLIT_HI 1000000,-1000000,0\nHAL_TINT 1000000,0,0\n"
            "HALATION 1000000 1000000 1000000\n[RAW]\nEXPOSURE -32\n";
        bake_valid(valid,strlen(valid));
    }
    for(i=1;i<(size_t)argc;i++) {
        FILE *f=fopen(argv[i],"rb");
        long n;
        char *p;
        check(f!=NULL,"stock fixture opens");
        check(fseek(f,0,SEEK_END)==0,"stock fixture seeks"); n=ftell(f);
        check(n>0 && n<65536,"stock fixture is bounded");
        rewind(f); p=malloc((size_t)n); check(p!=NULL,"stock fixture allocation");
        check(fread(p,1,(size_t)n,f)==(size_t)n,"stock fixture reads"); fclose(f);
        bake_valid(p,(size_t)n); free(p);
    }
    printf("stock numeric safety: %u checks passed\n",checks);
    return 0;
}
