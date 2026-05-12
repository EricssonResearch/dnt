
#include "testing.h"

#include "header.h"
#include "log.h"
#include "notification.h"

TEST_INIT("Header API");

// XXX stubs for stuff that we depend on but don't need
bool notification_push_event(const char *source, NotificationLevel level, struct JsonValue *message)
    { (void)source; (void)level; (void)message; return false; }
bool notification_register_source(const char *name, notification_pull_fn *callback, void *self, unsigned period_ms)
    { (void)name; (void)callback; (void)self; (void)period_ms; return true; }
// XXX end stubs

// compatibility:
//      they must have the same number of bits
//      their offset within a byte must be the same
//TODO for 1 bit values it would be simple to write an offset adaptor

#define FOR_ALL                                                                 \
    for (unsigned foffs=0; foffs<=32; foffs++) {                                \
        for (unsigned fcnt=1; fcnt<=32; fcnt++) {                               \
            for (unsigned voffs=0; voffs<=32; voffs++) {                        \
                for (unsigned vcnt=1; vcnt<=32; vcnt++) {                       \
                    struct HeaderField field;                                   \
                    field.bitoffset = foffs;                                    \
                    field.bitcount = fcnt;                                      \
                    struct Value value;                                         \
                    value.bitoffset = voffs;                                    \
                    value.bitcount = vcnt;                                      \
                    bool compatible = fcnt == vcnt && (foffs%8) == (voffs%8);

#define END_FOR_ALL                                                             \
                }                                                               \
            }                                                                   \
        }                                                                       \
    }

static void test_writer(void)
{
    log_set_level("HEADER", LOGGING_NONE);

    FOR_ALL
        value_consumer *writer = header_get_field_writer(&field, &value);
        OK(compatible == (writer != NULL), "compatible %s source %u %u target %u %u",
                compatible?"true":"false", foffs, fcnt, voffs, vcnt);
    END_FOR_ALL
}

static void test_reader(void)
{
    log_set_level("HEADER", LOGGING_NONE);

    FOR_ALL
        value_producer *reader = header_get_field_reader(&value, &field);
        OK(compatible == (reader != NULL), "compatible %s source %u %u target %u %u",
                compatible?"true":"false", voffs, vcnt, foffs, fcnt);
    END_FOR_ALL
}

static void test_comparator(void)
{
    log_set_level("HEADER", LOGGING_NONE);

    FOR_ALL
        // note: currently we can only compare to constant
        value_comparator *compare = header_get_field_comprator(&field, &value);
        OK(compatible == (compare != NULL), "compatible %s source %u %u target %u %u",
                compatible?"true":"false", foffs, fcnt, voffs, vcnt);
    END_FOR_ALL
}

TEST_CASES = {
    {"writer", test_writer},
    {"reader", test_reader},
    {"comparator", test_comparator},
    {NULL, NULL}
};
