// Time zones for the time settings: label, POSIX rule, IANA names (to
// suggest the browser's zone). Same list as the setup wizard
// (lgal_core/www/setup.html) - keep both in step.
export const ZONES = [
  ['UTC','UTC0',['Etc/UTC','UTC']],
  ['Europe/London, Dublin','GMT0BST,M3.5.0/1,M10.5.0',['Europe/London','Europe/Dublin','Europe/Guernsey','Europe/Jersey','Europe/Isle_of_Man']],
  ['Europe/Lisbon','WET0WEST,M3.5.0/1,M10.5.0',['Europe/Lisbon','Atlantic/Canary','Atlantic/Madeira']],
  ['Europe/Berlin, Vienna, Zurich, Paris, Rome, Madrid …','CET-1CEST,M3.5.0,M10.5.0/3',['Europe/Berlin','Europe/Vienna','Europe/Zurich','Europe/Paris','Europe/Rome','Europe/Madrid','Europe/Amsterdam','Europe/Brussels','Europe/Luxembourg','Europe/Copenhagen','Europe/Oslo','Europe/Stockholm','Europe/Warsaw','Europe/Prague','Europe/Budapest','Europe/Bratislava','Europe/Ljubljana','Europe/Zagreb','Europe/Belgrade','Europe/Vaduz','Europe/Monaco','Europe/Malta','Europe/Andorra','Europe/San_Marino','Europe/Vatican','Europe/Busingen','Europe/Gibraltar','Europe/Sarajevo','Europe/Skopje','Europe/Podgorica','Europe/Tirane','Arctic/Longyearbyen']],
  ['Europe/Helsinki, Athens, Kyiv, Bucharest …','EET-2EEST,M3.5.0/3,M10.5.0/4',['Europe/Helsinki','Europe/Athens','Europe/Kyiv','Europe/Kiev','Europe/Bucharest','Europe/Sofia','Europe/Riga','Europe/Tallinn','Europe/Vilnius','Europe/Mariehamn','Asia/Nicosia','Europe/Nicosia']],
  ['Europe/Istanbul','<+03>-3',['Europe/Istanbul','Asia/Istanbul']],
  ['Europe/Moscow','MSK-3',['Europe/Moscow','Europe/Simferopol']],
  ['Asia/Dubai','<+04>-4',['Asia/Dubai','Asia/Muscat']],
  ['Asia/Kolkata','IST-5:30',['Asia/Kolkata','Asia/Calcutta']],
  ['Asia/Bangkok, Jakarta','<+07>-7',['Asia/Bangkok','Asia/Jakarta','Asia/Ho_Chi_Minh','Asia/Saigon']],
  ['Asia/Singapore, Kuala Lumpur','<+08>-8',['Asia/Singapore','Asia/Kuala_Lumpur','Asia/Manila']],
  ['Asia/Shanghai, Hong Kong, Taipei','CST-8',['Asia/Shanghai','Asia/Hong_Kong','Asia/Taipei','Asia/Macau']],
  ['Asia/Tokyo, Seoul','JST-9',['Asia/Tokyo','Asia/Seoul']],
  ['Australia/Perth','AWST-8',['Australia/Perth']],
  ['Australia/Adelaide','ACST-9:30ACDT,M10.1.0,M4.1.0/3',['Australia/Adelaide','Australia/Broken_Hill']],
  ['Australia/Brisbane','AEST-10',['Australia/Brisbane','Australia/Lindeman']],
  ['Australia/Sydney, Melbourne','AEST-10AEDT,M10.1.0,M4.1.0/3',['Australia/Sydney','Australia/Melbourne','Australia/Canberra','Australia/Hobart']],
  ['Pacific/Auckland','NZST-12NZDT,M9.5.0,M4.1.0/3',['Pacific/Auckland']],
  ['Africa/Johannesburg','SAST-2',['Africa/Johannesburg']],
  ['America/Sao_Paulo','<-03>3',['America/Sao_Paulo','America/Argentina/Buenos_Aires','America/Buenos_Aires']],
  ['America/New_York, Toronto','EST5EDT,M3.2.0,M11.1.0',['America/New_York','America/Toronto','America/Detroit','America/Indiana/Indianapolis']],
  ['America/Chicago','CST6CDT,M3.2.0,M11.1.0',['America/Chicago','America/Winnipeg']],
  ['America/Denver','MST7MDT,M3.2.0,M11.1.0',['America/Denver','America/Edmonton','America/Boise']],
  ['America/Phoenix','MST7',['America/Phoenix']],
  ['America/Los_Angeles, Vancouver','PST8PDT,M3.2.0,M11.1.0',['America/Los_Angeles','America/Vancouver']],
  ['America/Anchorage','AKST9AKDT,M3.2.0,M11.1.0',['America/Anchorage']],
  ['Pacific/Honolulu','HST10',['Pacific/Honolulu']],
];

// Index of a POSIX rule in ZONES, -1 if it is not in the list
export const zoneIndex = posix => ZONES.findIndex(z => z[1] === posix);

// Index of the browser's time zone in ZONES, -1 if unknown
export function browserZone() {
  let name = '';
  try { name = Intl.DateTimeFormat().resolvedOptions().timeZone || ''; } catch (e) { /* old browser */ }
  return ZONES.findIndex(z => z[2].includes(name));
}
