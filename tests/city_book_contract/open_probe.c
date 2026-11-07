/* Exact current City source and readers; reuse only synthetic helper definitions. */
#define main unused_cache_main
#include "cache_probe.c"
#undef main

static void write_piece(FILE *f, int piece, int wii, int kind)
{
    uint8_t raw[1024];
    fixture(raw, wii, kind == 2, piece == 1);
    if (piece == 1) rename_fixture(raw, "WorldData", "Answer", "Root:Present");
    if (kind == 1) raw[0] = 'X';
    if (kind == 3) {
        char longkey[80]; memset(longkey, 'x', 79); longkey[79] = 0;
        fixture(raw, wii, 0, 0);
        rename_fixture(raw, "WorldData", "ObjectTemplateBook", longkey);
        raw[0x2d4] = PK_NAME; u32(raw + 0x2dc, 3, wii);
    }
    assert(!fseek(f, piece * 1024, SEEK_SET));
    assert(fwrite(raw, 1, sizeof raw, f) == sizeof raw); fflush(f);
}

static void failed_open(const Vol *v, const char *name, City *prior, int expected)
{
    size_t bytes = ledger_bytes, owners = ledger_live;
    int status = -99;
    assert(!city_open_checked(v, name, &status));
    assert(status == expected && active_city == prior);
    assert(ledger_bytes == bytes && ledger_live == owners);
}

static void sweep_open(const Vol *v, const char *name, City *prior, const char *label)
{
    size_t bytes = ledger_bytes, owners = ledger_live, begin = ledger_calls;
    int status = -99;
    City *c = city_open_checked(v, name, &status);
    assert(c && !status && !city_load_failed(c) && active_city == c);
    size_t count = ledger_calls - begin;
    city_close(c); assert(!active_city);
    city_activate(prior);
    assert(ledger_bytes == bytes && ledger_live == owners);
    for (size_t fault = 1; fault <= count; fault++) {
        size_t hits = ledger_fail_hits;
        ledger_fail_at = ledger_calls + fault;
        failed_open(v, name, prior, 1);
        ledger_reset_fault();
        assert(ledger_fail_hits == hits + 1);
        /* The failed owner was destroyed. A fresh attempt succeeds and selects itself. */
        status = -99;
        c = city_open_checked(v, name, &status);
        assert(c && !status && active_city == c);
        city_close(c); city_activate(prior);
        assert(ledger_bytes == bytes && ledger_live == owners);
    }
    printf("checked_open label=%s allocations=%zu every_fault_status1=1 initial_city_calloc=1 final_model_key_calloc=1 zero_failed_owners=1 fresh_retry=1 resolver_restore=1\n", label, count);
}

static void synthetic_open(void)
{
    for (int wii = 0; wii < 2; wii++) {
        FILE *f = tmpfile(); assert(f);
        for (int i = 0; i < 3; i++) write_piece(f, i, wii, 0);
        VolFile rows[3] = {{0}};
        const char *names[] = { "Example.bdp", "Example_Props.bdp", "Present.bdp" };
        for (int i = 0; i < 3; i++) { rows[i].name = names[i]; rows[i].size = 1024; rows[i].offset = i * 1024; }
        Vol v = {0}; v.f = f; v.files = rows; v.count = 3;
        City *prior = fresh(&v); city_activate(prior);
        LoadedBook *child = load_book(prior, "Example_Props"); assert(child);
        Group inherited = book_group(&child->book, "WorldData");
        assert(group_num(inherited, "Answer", -1) == 42);
        sweep_open(&v, "Example", prior, wii ? "synthetic-wii" : "synthetic-ps2");
        assert(group_num(inherited, "Answer", -1) == 42);

        /* Absent City never allocates; absent required Props cleans geometry, out=0. */
        v.count = 0; size_t calls = ledger_calls;
        ledger_fail_at = calls + 1; failed_open(&v, "Example", prior, 0);
        assert(ledger_calls == calls); ledger_reset_fault();
        v.count = 1; failed_open(&v, "Example", prior, 0);
        v.count = 2; int status = -99;
        City *c = city_open_checked(&v, "Example", &status);
        assert(c && !status); city_close(c); city_activate(prior); v.count = 3;

        /* Presence must not be collapsed into absence, including inherited parent I/O. */
        for (int piece = 0; piece < 3; piece++) {
            uint64_t offset = rows[piece].offset; rows[piece].offset = 3072;
            failed_open(&v, "Example", prior, 1); rows[piece].offset = offset;
            write_piece(f, piece, wii, 1); failed_open(&v, "Example", prior, 1);
            write_piece(f, piece, wii, 0);
            if (piece) { write_piece(f, piece, wii, 2); failed_open(&v, "Example", prior, 1); write_piece(f, piece, wii, 0); }
        }
        write_piece(f, 1, wii, 3); failed_open(&v, "Example", prior, 1); write_piece(f, 1, wii, 0);
        char longname[80]; memset(longname, 'x', 79); longname[79] = 0;
        failed_open(&v, longname, prior, 1); failed_open(&v, "", prior, 1);
        failed_open(&v, NULL, prior, 1); failed_open(NULL, "Example", prior, 1);

        /* A different failed City must neither reset nor contaminate the prior owner. */
        prior->load_failed = 1; ledger_fail_read_name = "Example_Props.bdp";
        failed_open(&v, "Example", prior, 1); assert(city_load_failed(prior));
        city_reset_load_failure(prior);
        assert(group_num(inherited, "Answer", -1) == 42);
        size_t bytes = ledger_bytes, owners = ledger_live;
        ledger_fail_read_name = "Present.bdp";
        assert(!city_open_checked(&v, "Example", NULL) && active_city == prior);
        assert(ledger_bytes == bytes && ledger_live == owners);
        ledger_fail_read_name = "Example.bdp";
        assert(!city_open(&v, "Example") && active_city == prior);
        assert(ledger_bytes == bytes && ledger_live == owners);
        c = city_open(&v, "Example"); assert(c && active_city == c);
        city_close(prior); assert(active_city == c); city_close(c); assert(!active_city);
        /* Also restore an initially detached resolver. */
        ledger_fail_read_name = "Present.bdp"; failed_open(&v, "Example", NULL, 1);
        fclose(f); assert(!ledger_bytes && !ledger_live);
        printf("checked_open format=%s missing_required_status0=1 missing_parent_optional=1 present_short_corrupt_nobook_status1=1 invalid_key_status1=1 previous_sticky_preserved=1 legacy_and_null_out=1\n", wii ? "Wii" : "PS2");
    }
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0); synthetic_open();
    if (argc > 1) {
        Vol v; assert(!vol_open(&v, argv[1]));
        City *prior = city_open(&v, "LasVegas"); assert(prior);
        sweep_open(&v, "LasVegas", prior, "real-ps2-lasvegas");
        assert(city_game_block(prior, 0));
        city_close(prior); vol_cleanup(&v);
    }
    ledger_report("checked_open_all_closed"); assert(!ledger_bytes && !ledger_live); return 0;
}
