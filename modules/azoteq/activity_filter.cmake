# Keep the pinned ZMK activity implementation, replacing only its input callback.
# Fail loudly if an upstream update changes the callback; never silently restore
# telemetry wakeups or compile two activity implementations.
set(toucan_activity_original "${APPLICATION_SOURCE_DIR}/src/activity.c")
file(READ "${toucan_activity_original}" toucan_activity_source)
set(toucan_activity_old "static void activity_input_listener(struct input_event *ev) { k_work_submit(&note_activity_work); }")
set(toucan_activity_new [=[#include <toucan/input_activity.h>
static void activity_input_listener(struct input_event *ev) {
    if (toucan_input_is_user_activity(ev->type, ev->code)) {
        k_work_submit(&note_activity_work);
    }
}]=])
string(FIND "${toucan_activity_source}" "${toucan_activity_old}" toucan_activity_index)
if(toucan_activity_index EQUAL -1)
  message(FATAL_ERROR "Toucan activity filter: unsupported ZMK activity callback")
endif()
string(REPLACE "${toucan_activity_old}" "${toucan_activity_new}"
       toucan_activity_source "${toucan_activity_source}")
set(toucan_activity_generated "${CMAKE_CURRENT_BINARY_DIR}/toucan_activity.c")
file(WRITE "${toucan_activity_generated}" "${toucan_activity_source}")
set_source_files_properties("${toucan_activity_original}" TARGET_DIRECTORY app
                           PROPERTIES HEADER_FILE_ONLY TRUE)
target_sources(app PRIVATE "${toucan_activity_generated}")
