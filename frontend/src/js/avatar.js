// js/avatar.js
import * as BABYLON from '@babylonjs/core';
import '@babylonjs/loaders/glTF';
import '@babylonjs/inspector'; // 필요하다면 Inspector 활성화

const AvatarService = {
    engine: null,
    scene: null,
    camera: null,
    light: null,
    avatarMesh: null,
    morphTargetManager: null,

    initialize(canvasElement, avatarUrl) { // avatarUrl 파라미터 추가
        this.engine = new BABYLON.Engine(canvasElement, true);
        this.scene = new BABYLON.Scene(this.engine);
        this.scene.clearColor = new BABYLON.Color4(0, 0, 0, 0); // RGBA: 완전 투명

        // 카메라 설정
        const avatarTarget = new BABYLON.Vector3(0, 1.7, 0);

        // ✅ 카메라 생성 (avatarTarget 기준으로 회전/이동)
        this.camera = new BABYLON.ArcRotateCamera(
            "camera",
            Math.PI / 2,           // 회전 각도: 오른쪽에서 정면
            Math.PI / 2,           // 높이 각도: 수직 아래
            1.3,                   // 거리
            avatarTarget,
            this.scene
        );
        this.camera.attachControl(canvasElement, true);
        this.camera.setTarget(avatarTarget);
        this.camera.inputs.attached.pointers.angularSensibilityX = 4000;
        this.camera.lowerRadiusLimit = 1.2;
        this.camera.upperRadiusLimit = 3.0;
        this.camera.lowerAlphaLimit = Math.PI * 0.25;
        this.camera.upperAlphaLimit = Math.PI * 0.75;
        this.camera.lowerBetaLimit = Math.PI / 2;
        this.camera.upperBetaLimit = Math.PI / 2;
        this.camera.wheelDeltaPercentage = 0.01;
        this.camera.inputs.attached.pointers.buttons = [0];

        // 조명 설정
        this.light = new BABYLON.HemisphericLight("light", new BABYLON.Vector3(0, 1, 0), this.scene);
        this.light.intensity = 0.7;

        // Inspector 활성화
        // this.scene.debugLayer.show();

        const modelPath = "/models/";
        const filename = "avatar.glb";

        BABYLON.SceneLoader.ImportMeshAsync("", modelPath, filename, this.scene).then((result) => {
            this.avatarMesh = result.meshes[0];
            this.avatarMesh.position = new BABYLON.Vector3(0, 0, 0);
            this.avatarMesh.rotation = new BABYLON.Vector3(0, Math.PI, 0);

            // 🧠 Morph Target 설정
            result.meshes.forEach(mesh => {
            if (mesh.morphTargetManager) {
                console.log("✅ Found morph targets:", mesh.morphTargetManager.numTargets);
                this.morphTargetManager = mesh.morphTargetManager;
            }
            });

            if (!this.morphTargetManager) {
            console.warn("❌ Avatar mesh does not have morph targets for lip sync.");
            }

            this.morphTargetManager = this.avatarMesh.morphTargetManager;
            if (this.morphTargetManager) {
                console.log(`Avatar loaded with ${this.morphTargetManager.numTargets} morph targets.`);
                for (let i = 0; i < this.morphTargetManager.numTargets; i++) {
                    this.morphTargetManager.getTarget(i).influence = 0;
                }
            } else {
                console.warn("Avatar mesh does not have morph targets for lip sync.");
            }
        }).catch((error) => {
            console.error("Error loading avatar from URL:", error);
        });

        // 렌더링 루프 시작
        this.engine.runRenderLoop(() => {
            this.scene.render();
        });

        // 창 크기 변경 시 렌더링 크기 조절
        window.addEventListener('resize', () => {
            this.engine.resize();
        });

        console.log("Babylon.js scene initialized.");
    },

    // 립싱크 데이터 적용 함수 (Viseme 기반)
    updateLipSync(lipSyncData) {
        if (!this.morphTargetManager || !lipSyncData || lipSyncData.type !== 'lipsync' || !lipSyncData.visemes) {
            return;
        }

        const visemes = lipSyncData.visemes;
        const currentTime = performance.now() / 1000; // 현재 시간 (초) - 애니메이션 진행 상황에 따라 업데이트 필요

        // 현재 시간에 해당하는 viseme 찾기 (예시 - 정확한 타이밍 처리는 별도 구현 필요)
        for (let i = 0; i < visemes.length; i++) {
            const viseme = visemes[i];
            const startTime = viseme.time;
            const endTime = visemes[i + 1] ? visemes[i + 1].time : startTime + 0.1; // 다음 viseme가 없으면 짧게 유지

            if (currentTime >= startTime && currentTime < endTime) {
                this.applyViseme(viseme.name);
                return; // 현재 viseme 적용 후 종료
            }
        }
        // 현재 시간에 해당하는 viseme이 없으면 기본 입 모양 유지 (선택 사항)
        this.resetLipSync();
    },

    // 특정 Viseme에 따른 블렌드 셰이프 적용
    applyViseme(visemeName) {
        if (!this.morphTargetManager) {
            return;
        }

        // Ready Player Me 아바타의 블렌드 셰이프 이름과 Viseme 매핑 (확인 필요!)
        switch (visemeName) {
            case 'PP': // 입술 닫힘 (P, B, M)
                this.setMorphTargetInfluence('mouthClose', 1);
                this.setMorphTargetInfluence('jawOpen', 0);
                break;
            case 'AA': // 크게 벌린 입 (A)
                this.setMorphTargetInfluence('mouthOpen', 1);
                this.setMorphTargetInfluence('jawOpen', 0.8);
                break;
            case 'EE': // 약간 벌리고 양쪽으로 당긴 입 (E)
                this.setMorphTargetInfluence('mouthSmile', 0.8);
                this.setMorphTargetInfluence('mouthOpenSlight', 0.5);
                this.setMorphTargetInfluence('jawOpen', 0.2);
                break;
            case 'IH': // 약간 벌린 입 (I)
                this.setMorphTargetInfluence('mouthOpenSlight', 0.6);
                this.setMorphTargetInfluence('jawOpen', 0.3);
                break;
            case 'OH': // 동그랗게 벌린 입 (O)
                this.setMorphTargetInfluence('mouthRound', 1);
                this.setMorphTargetInfluence('mouthOpenMid', 0.6);
                this.setMorphTargetInfluence('jawOpen', 0.4);
                break;
            case 'UU': // 오므린 입 (U)
                this.setMorphTargetInfluence('mouthPucker', 1);
                this.setMorphTargetInfluence('mouthClose', 0.2);
                this.setMorphTargetInfluence('jawOpen', 0.1);
                break;
            case 'SS': // 치찰음 (S, Z)
            case 'SH': // 치찰음 (SH, CH)
            case 'TH': // 치찰음 (TH)
            case 'DD': // 파열음 (D, T)
            case 'RR': // 굴리는 소리 (R)
            case 'LL': // 설측음 (L)
            case 'KK': // 연구개 파열음 (K, G)
            case 'NG': // 비음 (NG)
            case 'FV': // 순치 마찰음 (F, V)
            case 'W':  // 반모음 (W)
            case 'YY': // 반모음 (Y)
            case 'sil': // 묵음
            default:
                this.resetLipSync();
                break;
        }
    },

    // 특정 이름의 Morph Target 영향력 설정
    setMorphTargetInfluence(targetName, influence) {
        if (this.morphTargetManager) {
            const targetIndex = this.morphTargetManager.getTargetIndexByName(targetName);
            if (targetIndex !== -1) {
                this.morphTargetManager.getTarget(targetIndex).influence = BABYLON.Scalar.Clamp(influence, 0.0, 1.0);
            }
        }
    },

    // 모든 립싱크 관련 Morph Target 영향력 초기화
    resetLipSync() {
        if (this.morphTargetManager) {
            // Ready Player Me 아바타의 립싱크 관련 블렌드 셰이프 이름들을 알고 있어야 합니다.
            const lipSyncTargetNames = ['mouthClose', 'mouthOpen', 'mouthSmile', 'mouthFrown', 'mouthPucker', 'mouthRound', 'jawOpen', 'jawForward', 'jawLeft', 'jawRight', 'eyeBlinkLeft', 'eyeBlinkRight', 'mouthOpenSlight', 'mouthOpenMid']; // 예시, 실제 이름과 다를 수 있음
            lipSyncTargetNames.forEach(name => this.setMorphTargetInfluence(name, 0));
        }
    }
};

export { AvatarService };