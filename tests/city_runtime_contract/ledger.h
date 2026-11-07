#include <stddef.h>
#include <stdint.h>
#include "data.h"
extern size_t ledger_bytes, ledger_live, ledger_calls, ledger_fail_at, ledger_fail_hits, ledger_read_calls;
extern const char *ledger_fail_read_name;
void ledger_reset_fault(void);
void ledger_report(const char *label);
