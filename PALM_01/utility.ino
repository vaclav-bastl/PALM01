long curve_map(long value, const long *table_map, const size_t length) {
  float in_min = 0, in_max = 1.0f, out_min = 0, out_max = 1.0f;
  size_t in_start = 0;
  size_t out_start = length;
  for (size_t i = 0; i < length - 1; i++) {
    if (value >= table_map[in_start + i] && value <= table_map[in_start + i + 1]) {
      in_max = table_map[in_start + i + 1];
      in_min = table_map[in_start + i];
      out_max = table_map[out_start + i + 1];
      out_min = table_map[out_start + i];
      break;
    }
  }
  return map(value, in_min, in_max, out_min, out_max);
}


int stickyMap(int input, int minInput, int maxInput, int minOutput, int maxOutput, int &lastInput, int hysteresis) {
    // Check if the input value has moved significantly enough to trigger an update
    if (abs(input - lastInput) >= hysteresis) {
        // Rescale the input value from the input range to the output range
        if (minInput < maxInput) {
            // Normal range
            input = constrain(input, minInput, maxInput);
            lastInput = map(input, minInput, maxInput, minOutput, maxOutput);
        } else {
            // Inverted range
            input = constrain(input, maxInput, minInput);
            lastInput = map(input, maxInput, minInput, maxOutput, minOutput);
        }
    }

    // Return the last mapped value
    return lastInput;
}
