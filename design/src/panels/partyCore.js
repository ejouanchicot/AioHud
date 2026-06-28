/**
 * Rendu commun d'une LIGNE de joueur (party + alliances), calque XivParty.
 * XivParty gere 3 listes (index 0 = ta party, 1 = alliance 1, 2 = alliance 2), 0..6 membres.
 * Un membre d'ALLIANCE (m.alliance) : HP/MP/TP uniquement -> PAS de buffs ni d'actions
 * (le jeu ne les transmet pas), et couronne "chef d'alliance" dediee.
 */
import { asset } from '../core/config.js';
import { icon } from '../core/icons.js';
import { clamp, num } from '../core/util.js';
import { hpStep } from '../core/ramp.js';
// pool de buffs : la config choisit COMBIEN par joueur (ta party seulement)
export const BUFF_POOL = [40, 41, 43, 33, 251, 134, 13, 4, 5, 3, 30, 31, 32, 36, 37, 44, 45, 46, 39, 42, 0, 1, 2, 6, 7, 8, 9, 10, 11, 12, 14, 15];
const buffImg = (n) => `<img src="${asset(`buffIcons/${n}.png`)}">`;

// barre horizontale FINE : remplissage % (colore par type) + valeur au centre. Compact.
const bar = (kind, pct, val, cls = '') =>
  `<div class="gb gb-${kind} ${cls}"${kind === 'hp' ? ` style="--gc:${hpStep(pct)}"` : ''}>`
  + `<i style="width:${clamp(+pct, 0, 100).toFixed(1)}%"></i><span>${val}</span></div>`;

export const member = (m, i, cfg) => {
  const ally = !!m.alliance;                               // alliance : pas de buffs ni d'actions connus
  const hp = clamp(num(cfg[`p${i}_hp`], m.hp), 0, 100);
  const mp = clamp(num(cfg[`p${i}_mp`], m.mp), 0, 100);
  const tp = clamp(num(cfg[`p${i}_tp`], m.tp), 0, 3000);
  const nb = ally ? 0 : clamp(num(cfg[`p${i}_buffs`], m.buffs), 0, 32);
  const dead = hp <= 0;                                    // 0 HP -> KO
  const hpLow = (!dead && hp <= 25) ? 'low' : '';         // HP <= 25% (vivant) -> alarme rouge
  const tpReady = tp >= 1000 ? 'ready' : '';
  // puces de role a GAUCHE du badge (3 flags XivParty) : party leader (or), alliance leader (magenta),
  // quarter master (cyan). Colonne toujours rendue (largeur reservee) -> badges alignes meme sans puce.
  // marqueurs de role : chaque icone a sa boite serree, espacement regulier entre elles, et le GROUPE
  // est centre dans une zone a largeur fixe a gauche du badge (=> badges alignes sur toutes les lignes).
  const marks = []
    .concat(m.allianceLeader ? `<span class="pm-mark alead" title="Alliance Leader">${icon('crown', 11)}</span>` : [])
    .concat(m.leader ? `<span class="pm-mark lead" title="Party Leader">${icon('star', 11)}</span>` : [])
    .concat(m.qm ? `<span class="pm-mark qm" title="Quarter Master">${icon('dollar', 11)}</span>` : []);
  const marksHtml = `<div class="pm-marks">${marks.join('')}</div>`;
  // mort (KO) = ligne rouge/inactive (gere en CSS, pas d'icone). 2e ligne = action en cours (ta party).
  // Le slot de cast est TOUJOURS rendu (vide si rien) -> la place est reservee, le nom reste a la
  // meme position que les membres qui castent (lignes uniformes meme sans cast).
  const castInner = (!dead && !ally && m.cast) ? `${icon('cast', 9)} ${m.cast}` : '';
  return /* html */ `
  <div class="pm ${m.role}${dead ? ' dead' : ''}${ally ? ' ally' : ''}${m.self ? ' self' : ''}">
    ${ally ? '' : `<div class="pm-buffs">${BUFF_POOL.slice(0, nb).map(buffImg).join('')}</div>`}
    ${marksHtml}
    <div class="pm-badge"><span class="pm-jobtxt">${m.job}</span>${m.sub ? `<span class="pm-subtxt">${m.sub}</span>` : ''}</div>
    <div class="pm-main"><div class="pm-name">${m.name}</div>${ally ? '' : `<div class="pm-cast">${castInner}</div>`}</div>
    <div class="pm-gauges">
      ${bar('hp', hp, dead ? 0 : Math.round(m.maxHp * hp / 100), hpLow)}
      ${bar('mp', mp, Math.round(m.maxMp * mp / 100))}
      ${bar('tp', tp / 3000 * 100, tp, tpReady)}
    </div>
  </div>`;
};

// schema de config "par membre" partage : HP %, MP %, TP (+ buffs si ce n'est pas une alliance)
export function memberFields(m, i) {
  const f = [
    { key: `p${i}_sec`, type: 'section', label: `${m.name} · ${m.job}${m.sub ? '/' + m.sub : ''}` },
    { key: `p${i}_hp`, label: 'HP %', type: 'number', min: 0, max: 100, default: m.hp },
    { key: `p${i}_mp`, label: 'MP %', type: 'number', min: 0, max: 100, default: m.mp },
    { key: `p${i}_tp`, label: 'TP', type: 'number', min: 0, max: 3000, step: 50, default: m.tp },
  ];
  if (!m.alliance) f.push({ key: `p${i}_buffs`, label: 'Buffs', type: 'number', min: 0, max: 32, default: m.buffs });
  return f;
}
