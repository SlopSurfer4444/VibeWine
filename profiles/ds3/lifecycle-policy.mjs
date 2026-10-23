// Classifier must read the actual image and supply a visible next action with
// one of these intents. This is policy, not a vision model or a key script.
const intents = {
  'load-offline': {
    title: ['confirm-title'],
    notice: ['dismiss-visible-notice'],
    menu: ['select-offline', 'continue-offline'],
    loading: ['wait-loading'],
  },
  'safe-action': { world: ['safe-action'] },
  'normal-exit': {
    world: ['open-game-menu'],
    'exit-menu': ['select-quit-to-title', 'confirm-quit-to-title'],
    loading: ['wait-loading'],
    notice: ['dismiss-visible-notice'],
    title: ['dismiss-visible-notice'],
  },
  'prepare-desktop-quit': {
    title: ['select-desktop-quit'], menu: ['select-desktop-quit'],
    'quit-confirmation': ['select-desktop-quit'],
  },
  'quit-desktop': { title: ['quit-desktop'], menu: ['quit-desktop'], 'quit-confirmation': ['quit-desktop'] },
  'relaunch-ready': { notice: ['dismiss-visible-notice'], loading: ['wait-loading'] },
};
export const ds3Policy = Object.freeze({
  additionalKeys: Object.freeze(['e', 'q']),
  validateRoute(route) {
    for (const step of route.steps) {
      if (Object.entries(step.state).some(([name, value]) => value !== 0 && !['lx', 'ly'].includes(name))) {
        throw new Error('DS3 route permits only explicitly admitted left-stick movement');
      }
    }
  },
  decide({ goal, classification }) {
    const next = classification.next;
    if (!next || !intents[goal]?.[classification.state]?.includes(next.intent) ||
        typeof next.evidence !== 'string' || !next.evidence.trim()) {
      throw new Error(`DS3 has no admitted visible ${goal} decision`);
    }
    if (next.intent === 'wait-loading') return { type: 'wait', durationMs: 1000 };
    if (next.intent === 'continue-offline' && classification.offline !== true) {
      throw new Error('Offline mode must be visually established before Continue');
    }
    if (goal === 'safe-action' && (classification.offline !== true || classification.safe !== true || classification.hud !== true)) {
      throw new Error('Safe action requires a visually safe offline world');
    }
    if (goal === 'safe-action' && (next.action?.type !== 'key' ||
        !['w', 'a', 's', 'd', 'W', 'A', 'S', 'D'].includes(next.action.key) ||
        !(next.action.durationMs > 0 && next.action.durationMs <= 250))) {
      throw new Error('DS3 safe action must be one observed movement pulse at most 250 ms');
    }
    if (next.action?.type === 'secondary') throw new Error('DS3 game menus require visible key/click actions');
    return { type: 'action', action: next.action };
  },
});
