#ifndef DBG_H
#define DBG_H

//#define NODEBUG

#ifdef NODEBUG
#define DBG(x,...)
#else
#define DBG(x,...)		printk(x, ##__VA_ARGS__)
#endif

#define ERR(x,...)		printk(x, ##__VA_ARGS__)

#endif // DBG_H
