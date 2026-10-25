function token() {
  return shared.locked(function () {
    var t = shared.get('token'), at = Number(shared.get('at') || 0);
    if (t && Date.now() - at < 300000) return t;
    var r = http.get('https://tiles.example.org/api/token', { 'Referer': 'https://example.org/' });
    if (r.status !== 200) return null;
    var m = r.body.match(/"access_token":"([^"]+)"/);
    if (!m) return null;
    shared.set('token', m[1]); shared.set('at', String(Date.now()));
    return m[1];
  });
}
function getUrl(tile, ctx) {
  if (tile.z > 17) return null;               // no tiles above z17
  var t = token();
  if (!t) return null;                          // no token: no tile, try again later
  var h = Object.assign({}, ctx.requestHeaders, { 'Authorization': 'Bearer ' + t });
  return { url: template(ctx.urlBase), headers: h };
}
