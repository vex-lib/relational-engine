#ifndef EXCEPTION_TRY_H
#define EXCEPTION_TRY_H

// exception/try.h — the Try family forwarder.
//
// ONE include for the whole value-or-error primitive. Each payload type keeps
// its own `.h`/`.c` pair (the Single Class Per File Law); this forwarder is the
// ergonomic door for a consumer that wants all of it, mirroring lang/str.h.

#include "vex.h"
#include "exception/try_code.h"
#include "exception/try_value.h"
#include "exception/try_ptr.h"

#endif
