import { writeFileSync } from 'node:fs';
writeFileSync(new URL('../public/config.js', import.meta.url),
  'window.OpenSpeakConfig = {serverUrl:"",tailscaleOnly:true,cloudSignaling:true};\n');
