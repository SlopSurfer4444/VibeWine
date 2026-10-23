// Agent-assisted observation/action loop, not a game-state or gameplay verifier.
// Pass an already-bound CUA App. Recovery requires an explicit fresh binding
// and a new session. This module never launches, reacquires, or retries an App.
export function createSession(app, options = {}) {
  if (!options || typeof options !== 'object' || Array.isArray(options) ||
      Object.keys(options).some(key => key !== 'additionalKeys')) {
    throw new Error('Invalid session options');
  }
  const additionalKeys = options.additionalKeys === undefined ? [] : options.additionalKeys;
  if (!Array.isArray(additionalKeys) || additionalKeys.length > 52 ||
      Array.from(additionalKeys).some(key => typeof key !== 'string' || !/^[a-zA-Z]$/.test(key))) {
    throw new Error('additionalKeys must be at most 52 single ASCII letters');
  }
  const keys = new Set(['Up', 'Down', 'Left', 'Right',
    'w', 'a', 's', 'd', 'W', 'A', 'S', 'D', 'Return', 'Escape', 'Tab', ...additionalKeys]);
  let blocked = false, busy = false, current = null;

  function dimensions(value) {
    if (!value || !Number.isFinite(value.width) || value.width <= 0 ||
        !Number.isFinite(value.height) || value.height <= 0) {
      throw new Error('Explicit positive screenshot coordinate dimensions required');
    }
    return Object.freeze({ width: value.width, height: value.height });
  }

  async function capture(bounds) {
    const declared = dimensions(bounds);
    const screenshot = await app.getScreenshot({ emit: false });
    const ax = await app.getAXState({ disableDiffing: true });
    if (screenshot == null || ax == null) throw new Error('Incomplete observation');
    const token = Object.freeze({});
    current = { token, dimensions: declared };
    // Bounds are caller declarations, not dimensions validated from image bytes.
    return Object.freeze({ token, ax, screenshot, dimensions: declared });
  }

  async function guarded(operation) {
    if (blocked) throw new Error('Session blocked; explicitly bind a new App/session');
    if (busy) {
      blocked = true;
      current = null;
      throw new Error('Concurrent operation blocked the session');
    }
    busy = true;
    try {
      const result = await operation();
      if (blocked) throw new Error('Session blocked during operation');
      return result;
    } catch (error) {
      blocked = true;
      current = null;
      throw error;
    } finally {
      busy = false;
    }
  }

  function chooseAction(previous, action) {
    let perform;
    if (action.type === 'key') {
      const durationMs = action.durationMs ?? 100;
      if (!keys.has(action.key) || !Number.isFinite(durationMs) || durationMs <= 0 || durationMs > 1000) {
        throw new Error('Key or duration outside navigation/movement policy');
      }
      perform = () => app.pressKey(action.key, { durationMs });
    } else if (action.type === 'click') {
      const { x, y } = action;
      if (!Number.isFinite(x) || !Number.isFinite(y) || x < 0 || y < 0 ||
          x >= previous.dimensions.width || y >= previous.dimensions.height) {
        throw new Error('Click outside declared screenshot coordinate bounds');
      }
      perform = () => app.click([x, y]);
    } else if (action.type === 'secondary') {
      if (!Number.isInteger(action.index) || action.index < 0 ||
          typeof action.action !== 'string' || !action.action.trim()) throw new Error('Invalid AX action');
      perform = () => app.performSecondaryAction(action.index, action.action);
    } else throw new Error('Unsupported action');
    return perform;
  }

  return Object.freeze({
    get blocked() { return blocked; },
    observe(bounds) {
      return guarded(async () => {
        current = null;
        return capture(bounds);
      });
    },
    // One final visibly chosen Quit action may destroy its own window. No later
    // input/observation is allowed; process reconciliation supplies exit proof.
    finish(token, action) {
      return guarded(async () => {
        const previous = current;
        current = null;
        if (!previous || token !== previous.token) throw new Error('Stale or missing observation token');
        if (!action || typeof action !== 'object') throw new Error('Action required');
        const perform = chooseAction(previous, action);
        await perform();
        return Object.freeze({ delivered: true });
      }).finally(() => { blocked = true; current = null; });
    },
    act(token, action, nextBounds) {
      return guarded(async () => {
        const previous = current;
        current = null; // Single use even on validation or action failure.
        if (!previous || token !== previous.token) throw new Error('Stale or missing observation token');
        dimensions(nextBounds); // Validate before sending any input.
        if (!action || typeof action !== 'object') throw new Error('Action required');
        const perform = chooseAction(previous, action);
        // Even a failed input is followed by observation; no further input follows.
        let inputError, inputFailed = false;
        try { await perform(); } catch (error) { inputFailed = true; inputError = error; }
        const observation = await capture(nextBounds);
        if (inputFailed) {
          throw Object.assign(new Error('Input failed; session blocked', { cause: inputError }), { observation });
        }
        return observation;
      });
    },
  });
}
