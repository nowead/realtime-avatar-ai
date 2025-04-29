const path = require('path');

module.exports = {
  entry: './src/main.ts',             // 진입점 파일
  mode: 'development',                // 개발 모드 ('production'으로 변경하여 빌드)
  devtool: 'inline-source-map',       // 디버깅을 위한 소스맵
  module: {
    rules: [
      {
        test: /\.ts$/,                // .ts 파일 처리
        use: 'ts-loader',             // ts-loader 사용
        exclude: /node_modules/,      // node_modules 제외
      },
    ],
  },
  resolve: {
    extensions: ['.ts', '.js'],       // 해석할 확장자
  },
  output: {
    filename: 'bundle.js',            // 출력 파일 이름
    path: path.resolve(__dirname, 'public'), // 출력 디렉토리 (public 폴더)
  },
  devServer: {                        // 개발 서버 설정
    static: {
      directory: path.join(__dirname, 'public'), // 정적 파일 제공 경로
    },
    compress: true,                   // 압축 사용
    port: 8080,                       // 개발 서버 포트
    hot: true,                        // Hot Module Replacement 활성화
  },
};