
#ifndef R2_PARSETREE_H
#define R2_PARSETREE_H

struct Packet;
struct Pipeline;

struct ParseTree {
    //TODO members
};

//TODO parameters?
struct ParseTree *new_parsetree(void);

// @returns NULL
struct ParseTree *delete_parsetree(struct ParseTree *pt);

struct Pipeline *parsetree_process(struct ParseTree *pt, struct Packet *p);

#endif // R2_PARSETREE_H
