/* aj_app.h
 * Wrapper for the daemon process.
 * Contains all the devices and whatnot.
 * main() should delegate basically everything to this.
 */
 
#ifndef AJ_APP_H
#define AJ_APP_H

struct aj_app;

void aj_app_del(struct aj_app *app,int status);

struct aj_app *aj_app_new(int argc,char **argv);

/* Sleep, poll, update everything.
 * Returns >0 to proceed, 0 for normal exit, <0 for fatal errors.
 */
int aj_app_update(struct aj_app *app);

#endif
