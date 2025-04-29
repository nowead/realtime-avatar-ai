// src/main.ts
// BABYLON 타입을 사용하기 위해 import 추가
import * as BABYLON from '@babylonjs/core';
// 필요한 로더도 import 할 수 있습니다. GLTF 로더는 @babylonjs/loaders 패키지에 포함됩니다.
import "@babylonjs/loaders/glTF"; // .glb 파일을 로드하기 위해 필요

import { AvatarController } from './avatar_controller';
import { WebSocketClient } from './websocket_client';

class App {
    // BABYLON 타입을 명시적으로 사용
    private engine: BABYLON.Engine;
    private scene: BABYLON.Scene;
    private avatarController: AvatarController;
    private wsClient: WebSocketClient;

    // --- Configuration ---
    private readonly WEBSOCKET_URL = "ws://localhost:9001";
    private readonly SESSION_ID = `session_${Math.random().toString(36).substring(7)}`;
    private readonly AVATAR_MODEL_PATH = "YOUR_READY_PLAYER_ME_AVATAR_URL.glb"; // RPM URL 또는 로컬 경로 사용
    // --------------------

    constructor(canvas: HTMLCanvasElement) {
        // BABYLON.Engine 사용
        this.engine = new BABYLON.Engine(canvas, true, { preserveDrawingBuffer: true, stencil: true });
        this.scene = this.createScene(this.engine, canvas);
        this.avatarController = new AvatarController(this.scene);
        this.wsClient = new WebSocketClient(this.WEBSOCKET_URL, this.SESSION_ID);

        this.initialize();
    }

    // BABYLON 타입을 명시적으로 사용
    private createScene(engine: BABYLON.Engine, canvas: HTMLCanvasElement): BABYLON.Scene {
        const scene = new BABYLON.Scene(engine);
        scene.clearColor = new BABYLON.Color4(0.2, 0.2, 0.25, 1.0);

        const camera = new BABYLON.ArcRotateCamera("camera", -Math.PI / 2, Math.PI / 2.5, 3, new BABYLON.Vector3(0, 1, 0), scene);
        camera.attachControl(canvas, true);
        camera.wheelPrecision = 50;
        camera.lowerRadiusLimit = 1;
        camera.upperRadiusLimit = 10;

        const light = new BABYLON.HemisphericLight("light", new BABYLON.Vector3(0, 1, 0), scene);
        light.intensity = 0.8;
        scene.debugLayer.show({ embedMode: true });
        return scene;
    }

    private async initialize() {
        await this.avatarController.loadAvatar(this.AVATAR_MODEL_PATH);

        // WebSocket 리스너 설정 (이전과 동일)
         this.wsClient.on('open', () => {
            console.log("WebSocket connected and session registered.");
        });
         this.wsClient.on('avatar_sync', (data: { format: string, visemes: any[] }) => {
            console.log(`Received avatar_sync: ${data.visemes.length} visemes.`);
            const audioStartTime = performance.now();
            this.avatarController.playVisemeSequence(data.visemes, audioStartTime);
        });
         this.wsClient.on('binary_message', (blob: Blob) => {
            console.log(`Received binary data (potentially audio): ${blob.size} bytes`);
            // TODO: Process binary audio data
         });
         this.wsClient.on('close', (event) => {
            console.log("WebSocket connection closed.", event);
        });
         this.wsClient.on('error', (event) => {
            console.error("WebSocket error occurred.", event);
        });


        this.wsClient.connect();

        this.engine.runRenderLoop(() => {
            this.scene.render();
        });

        window.addEventListener("resize", () => {
            this.engine.resize();
        });
    }
}

// --- Entry Point ---
window.addEventListener("DOMContentLoaded", () => {
    const canvas = document.getElementById("renderCanvas") as HTMLCanvasElement;
    if (canvas) {
        new App(canvas);
    } else {
        console.error("Render canvas not found!");
    }
});