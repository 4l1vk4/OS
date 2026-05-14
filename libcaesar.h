#ifndef CAESAR_H
#define CAESAR_H

extern void caesar_key(unsigned char key);
extern void caesar(void* src, void* dst, int len);
extern void trigger_security_violation(void);

#endif
