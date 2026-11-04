static int complete;
int macdrv_display_capture_cleanup_complete_v1(void) { return complete; }
void wave7_test_set_capture_complete(int value) { complete = value; }
