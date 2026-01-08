function format() {
  const text = input.value;
  output.value = window.hellojson.formatJSON(text);
}

function minify() {
  const text = input.value;
  output.value = window.hellojson.minifyJSON(text);
}
