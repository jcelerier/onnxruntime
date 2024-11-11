import ctypes
import os
import platform


def get_nvidia_lib_paths():
    import site

    # Get the site-packages path where nvidia packages are installed
    site_packages_path = site.getsitepackages()[0]
    nvidia_path = os.path.join(site_packages_path, "nvidia")
    # Collect all directories under site-packages/nvidia that contain .dll files (for Windows)
    lib_paths = []
    if platform.system() == "Windows":  # Windows
        for root, _, files in os.walk(nvidia_path):
            if any(file.endswith(".dll") for file in files):
                lib_paths.append(root)
    elif platform.system() == "Linux":
        import re

        pattern = re.compile(r"\.so(\.\d+)?$")
        for root, _, files in os.walk(nvidia_path):
            for file in files:
                if pattern.search(file):
                    lib_paths.append(root)
    else:
        pass
    return lib_paths


def load_nvidia_libs():
    cuda_lib_paths = get_nvidia_lib_paths()
    if platform.system() == "Windows":
        for path in cuda_lib_paths:
            os.add_dll_directory(path)
    elif platform.system() == "Linux":
        for path in cuda_lib_paths:
            _ = ctypes.CDLL(path)
    else:
        pass
