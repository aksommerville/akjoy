#include "aj_app.h"

int main(int argc,char **argv) {
  struct aj_app *app=aj_app_new(argc,argv);
  if (!app) return 1;
  int status=0;
  while (1) {
    int err=aj_app_update(app);
    if (err<0) { status=1; break; }
    if (!err) { status=0; break; }
  }
  aj_app_del(app,status);
  return status;
}
