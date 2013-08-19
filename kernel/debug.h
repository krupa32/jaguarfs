#ifndef DBG_H
#define DBG_H

#define DBG(x,...)		printk(x, ##__VA_ARGS__)
#define ERR(x,...)		printk(x, ##__VA_ARGS__)

#endif // DBG_H
