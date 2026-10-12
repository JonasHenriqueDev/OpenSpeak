export class EventEmitter {
  #listeners = new Map();

  on(event, fn) {
    if (!this.#listeners.has(event)) this.#listeners.set(event, new Set());
    this.#listeners.get(event).add(fn);
    return () => this.off(event, fn);
  }

  once(event, fn) {
    const off = this.on(event, (...args) => {
      off();
      fn(...args);
    });
    return off;
  }

  off(event, fn) {
    this.#listeners.get(event)?.delete(fn);
  }

  emit(event, ...args) {
    for (const fn of [...(this.#listeners.get(event) ?? [])]) {
      try {
        fn(...args);
      } catch (err) {
        console.error(`[events] listener de "${event}" falhou`, err);
      }
    }
  }

  removeAllListeners() {
    this.#listeners.clear();
  }
}
