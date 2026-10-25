// Hand port of navionics.SonarChart.layer.zmp/GetUrlScript.txt (PascalScript; same as navionics.layer except layer=1).
// source's shared store for 5 min (cTTL div 2) under keys "0" (JSON) and "1" (unix time). Other maps: see the same logic.
var CREQ = 'transparent=true&ugc=true&layer=1&du=1&sd=2&sa=false';          // cReqParams
var TOKEN_URL = 'https://maps.garmin.com/marine/api/getNavionicsTokens';
var TTL_HALF = 300;                                                           // cTTL div 2 (cTTL = 10*60)

function nowUnix() { return Math.floor(Date.now() / 1000); }                  // GetUnixTime (true UTC)

function navToken() {
  return shared.locked(function () {
    var result = '';
    var json = shared.get('0');
    var stamp = shared.get('1');
    if (json !== undefined && stamp !== undefined) {
      var ts = Number(stamp);
      if (json !== '' && ts !== 0 && (TTL_HALF > nowUnix() - ts)) result = json;
    }
    if (result === '') {
      var r = http.get(TOKEN_URL, { 'Origin': 'https://maps.garmin.com', 'Referer': 'https://maps.garmin.com/' });
      if (r.status === 200) {
        result = r.body;
        shared.set('0', result);
        shared.set('1', String(nowUnix()));
      }
    }
    return result;
  });
}

function getUrl(tile, ctx) {
  var buf = navToken().split('\u0000')[0];                                    // R15: subject cut at first NUL
  var m1 = buf.match(/"configuration_token":"([\s\S]*)"/);                    // TRegExpr '.' matches \n (ModifierS on)
  var tok = m1 ? m1[1] : '';
  var m2 = buf.match(/"access_token":"([\s\S]*)",/);
  var bearer = m2 ? m2[1] : '';
  if (tok === '') return null;
  return {
    url: template(ctx.urlBase) + '?' + 'config=' + tok + '&' + CREQ,
    headers: {
      'Referer': 'https://maps.garmin.com/',
      'Origin': 'https://maps.garmin.com',
      'Authorization': 'Bearer ' + bearer
    }
  };
}
