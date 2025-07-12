# Five-key C hardware interface

`key_input.c` directly reads Linux `struct input_event` from the two verified input devices.

| Event code | Logical key |
|---:|---|
| `KEY_UP` (103) | `up` |
| `KEY_DOWN` (108) | `down` |
| `KEY_PREVIOUS` (412) | `left` |
| `KEY_NEXT` (407) | `right` |
| `KEY_POWER` (116, RK805) | `confirm` |

Build with `make`. `key_input_collect_five_keys()` accepts only press transitions (`value == 1`); Linux auto-repeat (`value == 2`) never satisfies a key test. The future C `manage` layer consumes `struct key_input_event` and emits the JSON protocol event with the same logical key name.
