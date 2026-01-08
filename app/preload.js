const { contextBridge } = require('electron');

contextBridge.exposeInMainWorld('hellojson', {
  formatJSON: (text) => {
    try {
      return JSON.stringify(JSON.parse(text), null, 2);
    } catch (e) {
      return e.message;
    }
  },
  minifyJSON: (text) => {
    try {
      return JSON.stringify(JSON.parse(text));
    } catch (e) {
      return e.message;
    }
  }
});
