const path = require('path');
const HtmlWebpackPlugin = require('html-webpack-plugin');
const CopyWebpackPlugin = require('copy-webpack-plugin');

module.exports = (env, argv) => {
  const isProduction = argv.mode === 'production';

  return {
    mode: isProduction ? 'production' : 'development',
    entry: './src/js/main.js', // 애플리케이션 진입점
    output: {
      filename: isProduction ? 'bundle.[contenthash].js' : 'bundle.js', // 번들 파일 이름 (프로덕션 시 캐싱 위해 contenthash 사용)
      path: path.resolve(__dirname, 'dist'), // 빌드 결과물 경로
      clean: true, // 빌드 시 dist 폴더 정리
    },
    module: {
      rules: [
        {
          test: /\.js$/, // .js 파일에 대해
          exclude: /node_modules/, // node_modules 제외
          use: {
            loader: 'babel-loader', // babel-loader 사용 (최신 JS -> 호환 JS)
          },
        },
        {
          test: /\.css$/i, // .css 파일에 대해
          use: ['style-loader', 'css-loader'], // style-loader, css-loader 사용 (CSS 처리)
        },
        {
          // 이미지, 폰트 등 다른 에셋 처리 룰 추가 가능
          test: /\.(png|svg|jpg|jpeg|gif)$/i,
          type: 'asset/resource',
        },
      ],
    },
    plugins: [
      new HtmlWebpackPlugin({
        template: './public/index.html', // HTML 템플릿 파일 경로
      }),
      new CopyWebpackPlugin({
        patterns: [
          {
            from: 'public/models', // 복사할 원본 디렉토리
            to: 'models', // 복사될 목적지 디렉토리 (dist/models)
            noErrorOnMissing: true // 원본 디렉토리 없어도 오류 발생 안 함
          },
          {
            from: 'src/js/audio-worklet-processor.js',
            to: 'worklets/audio-worklet-processor.js',
          },
          {
            from: 'src/css',
            to: 'css',
            noErrorOnMissing: true
          }
        ],
      }),
    ],
    devtool: isProduction ? 'source-map' : 'eval-source-map', // 개발 시 디버깅 편의성, 프로덕션 시 소스맵 제공
    devServer: {
      static: './dist', // 개발 서버가 파일을 서빙할 경로
      hot: true, // HMR(Hot Module Replacement) 활성화
      open: true, // 서버 시작 시 자동으로 브라우저 열기
      port: 8080, // 개발 서버 포트
    },
    // 성능 관련 경고 비활성화 (필요에 따라 조정)
    performance: {
        hints: false,
        maxEntrypointSize: 512000,
        maxAssetSize: 512000
    },
    // 일부 라이브러리 관련 경고 무시 (필요시 추가)
    ignoreWarnings: [/Failed to parse source map/],
  };
};