/* Test oracle: the pre-reservation ownership strategy. The rest of the runner
 * is freshly extracted from moves.c, so this is not an independent simulation.
 * It must never be linked into the application. */
static void enter(MoveRunner *r, int state, float time)
{
    const MoveState *s = &r->g->states[state];
    r->state = state;
    r->time = time;
    r->chosen = -1;
    free(r->in_window);
    free(r->sw_window);
    r->in_window = calloc(s->ntrans ? s->ntrans : 1, 1);
    r->sw_window = calloc(s->ntrans ? s->ntrans : 1, 1);
}

void moves_start(MoveRunner *r, const MoveGraph *g, int state)
{
    memset(r, 0, sizeof *r);
    r->g = g;
    r->from = r->via = -1;
    enter(r, state >= 0 && state < g->n ? state : 0, 0);
}

void moves_stop(MoveRunner *r)
{
    free(r->in_window);
    free(r->sw_window);
    memset(r, 0, sizeof *r);
}
