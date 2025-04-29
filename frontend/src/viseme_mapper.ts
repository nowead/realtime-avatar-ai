// src/viseme_mapper.ts
import type * as BABYLON from '@babylonjs/core'; // Babylon 타입을 가져오기 위해 import 추가
/**
 * 백엔드에서 받은 viseme ID를 아바타 모델 내의 특정 blendshape 이름이나
 * 인덱스로 매핑합니다.
 *
 * !!! 중요: 이 매핑은 사용하는 avatar.glb 모델에 매우 의존적입니다.
 * Blender나 BabylonJS Inspector 등으로 모델을 직접 확인하여
 * viseme에 해당하는 올바른 blendshape 이름/인덱스를 찾아야 합니다.
 */
export const VisemeMap: { [key: string]: string | number } = {
    // 예시 매핑 (실제 blendshape 이름/인덱스로 교체 필요)
    'v_SIL': 'mouth_neutral', // 침묵/기본 입 모양
    'v_PP': 'mouth_PP',      // 양순음 (p, b, m)
    'v_FF': 'mouth_FF',      // 순치음 (f, v)
    'v_TH': 'mouth_TH',      // 치간음 (th)
    'v_DD': 'mouth_DD',      // 치경음 (d, t, n, l)
    'v_kk': 'mouth_kk',      // 연구개음 (k, g, ng)
    'v_CH': 'mouth_CH',      // 경구개치경음 (ch, j, sh)
    'v_SS': 'mouth_SS',      // 치경 마찰음 (s, z)
    'v_nn': 'mouth_nn',      // 비음 (n) - DD와 유사할 수 있음
    'v_RR': 'mouth_RR',      // 권설음 (r)
    'v_aa': 'mouth_aa',      // 개모음 (a)
    'v_E':  'mouth_E',       // 중전설모음 (e)
    'v_I':  'mouth_I',       // 폐전설모음 (i)
    'v_O':  'mouth_O',       // 중후설모음 (o)
    'v_U':  'mouth_U',       // 폐후설모음 (u)
    // 백엔드가 보낼 수 있는 모든 viseme에 대한 매핑 추가
};

/**
 * viseme ID에 해당하는 blendshape 이름이나 인덱스를 가져옵니다.
 * @param visemeId Viseme ID 문자열 (예: 'v_aa')
 * @returns 해당하는 blendshape 이름/인덱스 또는 찾지 못한 경우 null.
 */
export function getBlendshapeNameForViseme(visemeId: string): string | number | null {
    return VisemeMap[visemeId] || null; // 매핑이 없으면 null 반환
}

/**
 * 이름으로 blendshape (morph target)의 인덱스를 가져옵니다.
 * @param morphTargetManager BabylonJS MorphTargetManager 인스턴스.
 * @param name Blendshape의 이름 또는 인덱스.
 * @returns Blendshape의 인덱스 또는 찾지 못한 경우 -1.
 */
export function getBlendshapeIndex(morphTargetManager: BABYLON.MorphTargetManager, name: string | number): number {
    if (typeof name === 'number') {
        // 맵이 이미 인덱스를 제공하는 경우
        if (name >= 0 && name < morphTargetManager.numTargets) {
            return name;
        }
    } else if (typeof name === 'string') {
        // 이름으로 인덱스 찾기
        for (let i = 0; i < morphTargetManager.numTargets; i++) {
            if (morphTargetManager.getTarget(i).name === name) {
                return i;
            }
        }
    }
    console.warn(`이름/인덱스 "${name}"에 해당하는 Blendshape를 찾을 수 없습니다.`);
    return -1; // 찾지 못한 경우 -1 반환
}