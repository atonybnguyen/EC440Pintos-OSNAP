#ifndef THREADS_FIXED
#define THREADS_FIXED

/* 17.14 fixed point */
typedef int fixed_t;
#define F (1<<14)

/* conversions */
#define INT_TO_FP(n) ((fixed_t)((n)*F))
#define FP_TO_INT_ZERO(x) ((x)/F)
#define FP_TO_INT_NEAR(x) ((x)>=0 ? ((x)+F/2)/F : ((x)-F/2)/F)

/* basic ops */
#define FP_ADD(x,y) ((x)+(y))
#define FP_SUB(x,y) ((x)-(y))
#define FP_ADD_INT(x,n) ((x)+((n)*F))
#define FP_SUB_INT(x,n) ((x)-((n)*F))
#define FP_MUL(x,y) ((fixed_t)(((int64_t)(x))*(y)/F))
#define FP_MUL_INT(x,n) ((x)*(n))
#define FP_DIV(x,y) ((fixed_t)((((int64_t)(x))*F)/(y)))
#define FP_DIV_INT(x,n) ((x)/(n))

#endif /* threads/fixed.h */