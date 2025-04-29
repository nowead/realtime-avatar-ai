// src/avatar_controller.ts
import * as BABYLON from '@babylonjs/core';
import { getBlendshapeNameForViseme, getBlendshapeIndex } from './viseme_mapper';

// Viseme 이벤트 데이터를 담는 간단한 인터페이스
interface VisemeEvent {
    timestamp_ms: number;
    viseme: string; // Viseme ID (예: 'v_aa')
}

export class AvatarController {
    private scene: BABYLON.Scene;
    private avatarMesh: BABYLON.AbstractMesh | null = null;
    private morphTargetManager: BABYLON.MorphTargetManager | null = null;
    // 현재 활성화된(애니메이션 중인) viseme들의 정보 저장
    private activeVisemeIndices: { [key: number]: { targetWeight: number, startTime: number, duration: number } } = {};
    private animationStartTime: number = 0; // 현재 애니메이션 시퀀스가 시작된 시간 (performance.now())

    constructor(scene: BABYLON.Scene) {
        this.scene = scene;
        this.setupAnimationLoop(); // 애니메이션 루프 설정
    }

    /**
     * 지정된 경로에서 아바타 모델을 로드합니다.
     * @param filePath 아바타 모델 경로 (예: "assets/avatar.glb")
     */
    async loadAvatar(filePath: string): Promise<void> {
        try {
            const result = await BABYLON.SceneLoader.ImportMeshAsync(null, "", filePath, this.scene);
            if (result.meshes.length > 0) {
                // 모프 타겟이 있는 첫 번째 메쉬를 아바타 메쉬로 가정 (필요시 조정)
                this.avatarMesh = result.meshes.find(mesh => mesh.morphTargetManager) || result.meshes[0];

                if (!this.avatarMesh) {
                     console.error("로드된 모델에서 적합한 메쉬를 찾을 수 없습니다.");
                     return;
                }

                 // MorphTargetManager 찾기
                 this.morphTargetManager = this.avatarMesh.morphTargetManager;

                 // 메쉬에 직접 없으면 자식 메쉬에서 찾기
                if (!this.morphTargetManager) {
                    console.warn("아바타 메쉬에 MorphTargetManager가 없습니다. 자식 메쉬를 검색합니다.");
                     this.avatarMesh.getChildMeshes(false).forEach(child => {
                         if (child.morphTargetManager && !this.morphTargetManager) {
                             this.morphTargetManager = child.morphTargetManager;
                             console.log(`자식 메쉬에서 MorphTargetManager 발견: ${child.name}`);
                         }
                     });
                }

                if (this.morphTargetManager) {
                    console.log(`아바타 로드 성공. Morph targets 개수: ${this.morphTargetManager.numTargets}`);
                    // 모든 blendshape 영향(influence)을 0으로 초기화
                    for (let i = 0; i < this.morphTargetManager.numTargets; i++) {
                        this.morphTargetManager.getTarget(i).influence = 0;
                    }
                } else {
                     console.error("로드된 아바타 또는 그 자식에서 MorphTargetManager를 찾을 수 없습니다.");
                }

                // 필요한 경우 아바타 위치 및 크기 조정
                this.avatarMesh.position = new BABYLON.Vector3(0, 0, 5); // 예시 위치
                // this.avatarMesh.scaling = new BABYLON.Vector3(1, 1, 1);

            } else {
                console.error("아바타 모델 로드 실패: 메쉬를 찾을 수 없습니다.");
            }
        } catch (error) {
            console.error("아바타 로딩 중 오류:", error);
        }
    }

    /**
     * Viseme 이벤트 시퀀스 처리를 시작합니다.
     * @param visemeEvents 타임스탬프 순으로 정렬된 viseme 이벤트 배열.
     * @param audioStartTime 관련된 오디오가 재생 시작된 시점의 타임스탬프 (performance.now()).
     */
    playVisemeSequence(visemeEvents: VisemeEvent[], audioStartTime: number) {
        if (!this.morphTargetManager) {
            console.warn("MorphTargetManager를 사용할 수 없어 viseme을 재생할 수 없습니다.");
            return;
        }

        this.animationStartTime = audioStartTime; // 애니메이션 시작 시간 기록
        this.activeVisemeIndices = {}; // 이전 시퀀스 정보 초기화

        console.log(`Viseme 시퀀스 시작 (${visemeEvents.length}개 이벤트). 오디오 시작 시간: ${audioStartTime.toFixed(0)}`);

        for (let i = 0; i < visemeEvents.length; i++) {
            const event = visemeEvents[i];
            const nextEvent = visemeEvents[i + 1]; // 다음 이벤트 확인 (지속 시간 계산용)

            const blendshapeName = getBlendshapeNameForViseme(event.viseme); // Viseme ID -> Blendshape 이름/인덱스 매핑
            if (blendshapeName === null) {
                console.warn(`Viseme 매핑 없음: ${event.viseme}`);
                continue;
            }

            const blendshapeIndex = getBlendshapeIndex(this.morphTargetManager, blendshapeName); // Blendshape 인덱스 찾기
            if (blendshapeIndex === -1) {
                continue; // getBlendshapeIndex에서 경고 로그 출력됨
            }

            const startTime = event.timestamp_ms; // Viseme 시작 시간 (오디오 기준 ms)
            // 지속 시간: 다음 viseme 시작 전까지, 마지막 viseme은 기본 지속 시간 사용
            const duration = nextEvent ? (nextEvent.timestamp_ms - startTime) : 100; // 마지막은 100ms (예시)

            // Viseme 타이밍 정보 저장
            this.activeVisemeIndices[blendshapeIndex] = {
                targetWeight: 1.0, // 목표 영향력 (보통 1.0)
                startTime: startTime,
                duration: Math.max(duration, 50) // 최소 지속 시간 보장 (예: 50ms)
            };

            //console.log(`Viseme 예약: ${event.viseme} (인덱스 ${blendshapeIndex}), 시작: ${startTime}ms, 지속: ${duration}ms`);
        }
    }

    /**
     * Blendshape를 업데이트하기 위한 Babylon.js 애니메이션 루프를 설정합니다.
     */
    private setupAnimationLoop() {
        this.scene.onBeforeRenderObservable.add(() => {
            // MorphTargetManager가 없거나 활성 viseme이 없으면 처리 중단
            if (!this.morphTargetManager || Object.keys(this.activeVisemeIndices).length === 0) {
                // 현재 애니메이션 시퀀스가 없을 때 기본 포즈로 돌리거나
                // 여기서 idle 애니메이션을 처리할 수 있습니다.
                // 일단은 예약된 viseme이 없으면 아무것도 하지 않음.
                return;
            }

            const currentTime = performance.now(); // 현재 시간 (페이지 로드 후 ms)
            const elapsedTimeMs = currentTime - this.animationStartTime; // 애니메이션 시퀀스 시작 후 경과 시간 (ms)

            // 현재 프레임의 viseme을 적용하기 전에 모든 influence 초기화 (점진적 감소)
            for (let i = 0; i < this.morphTargetManager.numTargets; i++) {
                const isActive = i in this.activeVisemeIndices &&
                                 elapsedTimeMs >= this.activeVisemeIndices[i].startTime &&
                                 elapsedTimeMs < (this.activeVisemeIndices[i].startTime + this.activeVisemeIndices[i].duration);

                 if (!isActive) { // 현재 활성 상태가 아닌 blendshape은 0으로 복원
                     const target = this.morphTargetManager.getTarget(i);
                     // 부드럽게 0으로 복원 (값 조절 가능)
                     target.influence = Math.max(0, target.influence - 0.15); // fade-out 속도 조절
                 }
            }


            // 현재 시간대에 활성화되어야 하는 viseme들의 influence 적용
            for (const indexStr in this.activeVisemeIndices) {
                const index = parseInt(indexStr, 10);
                const visemeData = this.activeVisemeIndices[index];

                const visemeStart = visemeData.startTime; // 이 viseme의 시작 시간 (오디오 기준 ms)
                const visemeEnd = visemeStart + visemeData.duration; // 이 viseme의 종료 시간 (오디오 기준 ms)

                const target = this.morphTargetManager.getTarget(index);
                if (!target) continue; // 해당 인덱스의 타겟이 없으면 건너뜀

                // 현재 경과 시간이 이 viseme의 활성 구간 내에 있는지 확인
                if (elapsedTimeMs >= visemeStart && elapsedTimeMs < visemeEnd) {
                    // --- 간단한 보간 (Interpolation) 예시 ---
                    const timeIntoViseme = elapsedTimeMs - visemeStart; // 이 viseme 시작 후 경과 시간
                    const progress = Math.min(1, timeIntoViseme / visemeData.duration); // 진행률 (0.0 ~ 1.0)

                    // 간단한 Ease-in-out 곡선 (필요에 따라 수정)
                    let weight = 0;
                    const peakTimeRatio = 0.3; // 최대 가중치 도달 시간 비율
                    const fadeStartRatio = 0.7; // 감소 시작 시간 비율

                    const peakTime = visemeData.duration * peakTimeRatio;
                    const fadeStartTime = visemeData.duration * fadeStartRatio;

                    if (timeIntoViseme < peakTime) { // Ease in
                         weight = visemeData.targetWeight * (timeIntoViseme / peakTime);
                    } else if (timeIntoViseme < fadeStartTime) { // 최대치 유지
                         weight = visemeData.targetWeight;
                    } else { // Ease out
                         const fadeDuration = visemeData.duration - fadeStartTime;
                         weight = visemeData.targetWeight * (1 - (timeIntoViseme - fadeStartTime) / fadeDuration);
                    }

                    target.influence = Math.max(0, Math.min(visemeData.targetWeight, weight)); // 계산된 가중치 적용
                    //console.log(`시간: ${elapsedTimeMs.toFixed(0)}ms | Viseme ${index} 활성 | Influence: ${target.influence.toFixed(2)}`);
                }
                // 비활성 상태로의 복원은 위쪽 초기화 루프에서 처리
            }

             // 완료된 viseme 정리 (선택 사항, 접근 방식에 따라 다름)
             // elapsedTimeMs > visemeEnd + fadeOutTime 인 경우 activeVisemeIndices에서 제거 가능
        });
    }
}