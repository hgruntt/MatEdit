{
  description = "MatEdit - PrimeXT material editor";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" ];
      forAllSystems = nixpkgs.lib.genAttrs systems;
    in
    {
      packages = forAllSystems (system:
        let
          pkgs = import nixpkgs { inherit system; };
          matedit = pkgs.stdenv.mkDerivation {
            pname = "matedit";
            version = "1.0.0";
            src = ./.;

            nativeBuildInputs = with pkgs; [ cmake pkg-config wayland-scanner ];
            buildInputs = with pkgs; [
              libGL
              libX11
              libXcursor
              libXi
              libXinerama
              libXrandr
              libxkbcommon
              wayland
              wayland-protocols
              libdecor
            ];

            cmakeFlags = [
              "-DCMAKE_BUILD_TYPE=Release"
              "-DGLFW_BUILD_X11=ON"
              "-DGLFW_BUILD_WAYLAND=ON"
            ];

            installPhase = ''
              runHook preInstall
              install -Dm755 MaterialEditor "$out/bin/MaterialEditor"
              runHook postInstall
            '';

            meta = with pkgs.lib; {
              description = "Material and texture editor for PrimeXT";
              platforms = platforms.linux;
              mainProgram = "MaterialEditor";
            };
          };
        in
        {
          default = matedit;
          matedit = matedit;
        });

      apps = forAllSystems (system: {
        default = {
          type = "app";
          program = "${self.packages.${system}.default}/bin/MaterialEditor";
        };
      });
    };
}
