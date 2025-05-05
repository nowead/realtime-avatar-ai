// js/avatar.js
const AvatarService = {
    engine: null,
    scene: null,
    camera: null,
    light: null,
    avatarMesh: null,
    morphTargetManager: null,

    initialize(canvasElement) {
        this.engine = new BABYLON.Engine(canvasElement, true);
        this.scene = new BABYLON.Scene(this.engine);

        // 카메라 설정
        this.camera = new BABYLON.ArcRotateCamera("camera", -Math.PI / 2, Math.PI / 2.5, 3, new BABYLON.Vector3(0, 1.5, 0), this.scene);
        this.camera.attachControl(canvasElement, true);
        this.camera.wheelDeltaPercentage = 0.01; // 줌 속도 조절

        // 조명 설정
        this.light = new BABYLON.HemisphericLight("light", new BABYLON.Vector3(0, 1, 0), this.scene);
        this.light.intensity = 0.7;

        // 바닥 (선택 사항)
        // const ground = BABYLON.MeshBuilder.CreateGround("ground", {width: 6, height: 6}, this.scene);

        // 아바타 로드 (GLB/GLTF 추천)
        BABYLON.SceneLoader.ImportMesh("", "models/", "avatar.glb", this.scene, (meshes) => {
            // 로드된 메쉬 중 아바타 메인 메쉬 찾기 (이름 또는 구조 기반)
            this.avatarMesh = meshes[0]; // 실제 구조에 맞게 수정 필요
            this.avatarMesh.position.y = 0; // 위치 조정

            // Morph Target Manager 찾기 (립싱크용)
            this.morphTargetManager = this.avatarMesh.morphTargetManager;
            if (this.morphTargetManager) {
                console.log(`Avatar loaded with ${this.morphTargetManager.numTargets} morph targets.`);
                // 초기 입 모양 설정 (예: 모든 영향력 0으로)
                for (let i = 0; i < this.morphTargetManager.numTargets; i++) {
                    this.morphTargetManager.getTarget(i).influence = 0;
                }
            } else {
                console.warn("Avatar mesh does not have morph targets for lip sync.");
            }
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

    // 립싱크 데이터 적용 함수
    updateLipSync(lipSyncData) {
        // lipSyncData 형식 가정: { type: 'lipsync', blendshapes: { 'mouthOpen': 0.8, 'mouthSmile': 0.2, ... } }
        // 또는 { type: 'lipsync', phonemes: [ { phoneme: 'A', start: 0.1, end: 0.3 }, ... ] }
        if (!this.morphTargetManager || !lipSyncData || lipSyncData.type !== 'lipsync') {
            return;
        }

        // 예시: Blendshape 데이터 직접 적용
        if (lipSyncData.blendshapes) {
            const blendshapes = lipSyncData.blendshapes;
            for (let i = 0; i < this.morphTargetManager.numTargets; i++) {
                const target = this.morphTargetManager.getTarget(i);
                const targetName = target.name; // Babylon.js에서 설정된 Morph Target 이름
                if (blendshapes.hasOwnProperty(targetName)) {
                    // 영향력(influence) 값 적용 (0.0 ~ 1.0)
                    target.influence = BABYLON.Scalar.Clamp(blendshapes[targetName], 0, 1);
                } else {
                    // 해당 blendshape 데이터 없으면 0으로 설정 (선택 사항)
                    target.influence = 0;
                }
            }
        }
        // 예시: Phoneme 데이터 기반 애니메이션 (더 복잡한 로직 필요)
        else if (lipSyncData.phonemes) {
            // 시간 기반으로 각 phoneme에 해당하는 morph target 애니메이션 구현
            console.warn("Phoneme-based lip sync animation not implemented yet.");
        }
    }
};