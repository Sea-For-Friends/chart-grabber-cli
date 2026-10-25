function getUrl(tile, ctx) {
  var token = shared.locked(function () {
    var t = shared.get("token");
    if (t === undefined) {
      t = http.get("http://127.0.0.1:8765/token").body;
      shared.set("token", t);
    }
    return t;
  });
  return ctx.urlBase + token + "/" + tile.z + "/" + tile.x + "/" + tile.y + ".png";
}
