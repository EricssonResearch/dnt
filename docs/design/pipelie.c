#include <stdio.h>
#include <string.h>
#include <stdlib.h>

struct packet {
	struct pipeline *iter;
};

struct action {
	void (* execute)(struct packet *p);
	void *state; //userdata
	char name[10];
};

struct pipeline {
	struct action *action;
	struct pipeline *next;
};

void execute(struct packet *p)
{
	if (!p->iter) {
		printf("'End of pipeline'\n");
		return;
//		printf("executing: %s\n", p->iter->action->name);
	}
	p->iter->action->execute(p);
}


void action_foo(struct packet *p)
{
	printf("Action foo -> ");
	p->iter = p->iter->next;
	execute(p);
}

void action_bar(struct packet *p)
{
	printf("Action bar -> ");
	p->iter = p->iter->next;
	execute(p);
}

void action_alma(struct packet *p)
{
	printf("Action alma -> ");
	p->iter = p->iter->next;
	execute(p);
}

//userdata / state
struct action_replicate_state {
	int num_pipelines;
	struct pipeline *pipes[10]; //just to avoid malloc
};

void action_replicate(struct packet *p)
{
	struct action_replicate_state *rep = p->iter->action->state;
	printf("Replicate -> ");
	for(int i = 0; i < rep->num_pipelines - 1; ++i) {
		struct packet *dup_p = calloc(1, sizeof(struct packet)); //memdup part1
		memcpy(dup_p, p, sizeof(struct packet)); //memdup part2
		dup_p->iter = rep->pipes[i];
		execute(dup_p);
	}
	p->iter = rep->pipes[rep->num_pipelines - 1];
	execute(p);
}

int main()
{
	// hopefully everything here done by the config parser and dynamically allocated
	struct action foo = { .execute = action_foo, .name = "foo" };
	struct action bar = { .execute = action_bar, .name = "bar" };
	struct action alma = { .execute = action_alma, .name = "alma" };
	struct action replicate = { .execute = action_replicate, .name = "repl"};

	struct pipeline *p_start, *p_repl1, *p_repl2;


	p_start = calloc(1, sizeof(struct pipeline));
	p_start->next = calloc(1, sizeof(struct pipeline));
	p_start->next->next = calloc(1, sizeof(struct pipeline));
	p_repl1 = calloc(1, sizeof(struct pipeline));
	p_repl1->next = calloc(1, sizeof(struct pipeline));
	p_repl2 = calloc(1, sizeof(struct pipeline));
	p_repl2->next = calloc(1, sizeof(struct pipeline));

	p_start->action = &foo;
	p_start->next->action = &bar;
	p_start->next->next->action = &replicate;
	struct action_replicate_state state = {.num_pipelines = 3, .pipes[0] = p_repl1, .pipes[1] = p_repl2, .pipes[2] = p_repl1};
	p_start->next->next->action->state = &state;
	p_repl1->action = &bar;
	p_repl1->next->action = &alma;
	p_repl2->action = &foo;
	p_repl2->next->action = &alma;


	struct packet pkt; //fresh packet received on some iface
	pkt.iter = p_start; // iter = match(pkt)
	execute(&pkt);
}
