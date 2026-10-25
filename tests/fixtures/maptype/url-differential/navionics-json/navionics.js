// Converted by hand from GetUrlScript.txt (navionics.zmp), 2026-10-17.
// T-0280: three lines changed against the parked copy (NUL cut, two dot-matches-newline patterns); see ../README.md.
// Pascal: token pair kept in Global vars 0 (JSON text) and 1 (unix time), reused for 300 s
// (cTTL div 2); refreshed from maps.garmin.com when stale; no tile when no configuration token.
var cReqParams = 'transparent=false&ugc=true&layer=0&du=1&sd=2&sa=false';
var cTTL = 10 * 60;

function nowUnix() {
  return Math.floor(Date.now() / 1000);
}

function checkTTL(ts) {
  // Pascal: (cTTL div 2) > GetUnixTime - ATimeStamp
  return Math.floor(cTTL / 2) > nowUnix() - ts;
}

// Pascal RegExprGetMatchSubStr(s, re, i): '' when there is no match
function matchSub(s, re, i) {
  var m = String(s).match(re);
  return (m && m[i] !== undefined) ? m[i] : '';
}

function getNavToken() {
  return shared.locked(function () {
    var result = '';
    var json = shared.get('0');
    var tsRaw = shared.get('1');
    if (json !== undefined && tsRaw !== undefined) {
      var ts = parseInt(tsRaw, 10) || 0;
      if (json !== '' && ts !== 0 && checkTTL(ts)) {
        result = json;
      }
    }
    if (result === '') {
      var r = http.get('https://maps.garmin.com/marine/api/getNavionicsTokens', {
        'Origin': 'https://maps.garmin.com',
        'Referer': 'https://maps.garmin.com/'
      });
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
  var buf = getNavToken().split('\u0000')[0]; // T-0280: Pascal subject ends at the first NUL (script.md R15)
  var navToken = matchSub(buf, /"configuration_token":"([\s\S]*)"/, 1);
  var navBearer = matchSub(buf, /"access_token":"([\s\S]*)",/, 1);
  if (navToken === '') {
    return null; // Pascal: ResultURL := '' (no tile)
  }
  return {
    url: template(ctx.urlBase) + '?' + 'config=' + navToken + '&' + cReqParams,
    headers: {
      'Referer': 'https://maps.garmin.com/',
      'Origin': 'https://maps.garmin.com',
      'Authorization': 'Bearer ' + navBearer
    }
  };
}
