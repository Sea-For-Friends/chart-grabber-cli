function getUrl(tile, ctx) {
  var q = "";
  for (var i = tile.z; i > 0; i--) {
    var mask = 1 << (i - 1);
    q += ((tile.x & mask) ? 1 : 0) + ((tile.y & mask) ? 2 : 0);
  }
  return ctx.urlBase + q + ".png";
}
