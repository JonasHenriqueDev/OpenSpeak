// Fullscreen controls follow activity, independently of :hover and overlay renders.
export function installFullscreenIdle(el, doc = document, timers = globalThis) {
  let timer = null;
  let dragging = false;
  const clear = () => { timers.clearTimeout(timer); timer = null; };
  const wake = () => {
    clear();
    el.classList.remove('fullscreen-idle');
    if (doc.fullscreenElement === el && !dragging) {
      timer = timers.setTimeout(() => {
        if (doc.fullscreenElement === el) el.classList.add('fullscreen-idle');
      }, 3000);
    }
  };
  const down = () => { dragging = true; wake(); };
  const up = () => { dragging = false; wake(); };
  const change = () => { dragging = false; wake(); };
  const events = ['pointermove', 'keydown', 'focusin', 'input'];
  for (const event of events) el.addEventListener(event, wake);
  el.addEventListener('pointerdown', down);
  doc.addEventListener('pointerup', up);
  doc.addEventListener('pointercancel', up);
  doc.addEventListener('fullscreenchange', change);
  return () => {
    clear();
    el.classList.remove('fullscreen-idle');
    for (const event of events) el.removeEventListener(event, wake);
    el.removeEventListener('pointerdown', down);
    doc.removeEventListener('pointerup', up);
    doc.removeEventListener('pointercancel', up);
    doc.removeEventListener('fullscreenchange', change);
  };
}
