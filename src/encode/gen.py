import numpy as np

def print_mat(m:np.ndarray):
    print('{')
    for i in range(m.shape[0]):
        for j in range(m.shape[1]):
            print(int(m[i,j]),end=', ')
        print('//')
    print('}')

def table_gen(n):
    mat=np.array(tuple(range(n**2))).reshape((n,n))
    print_mat(mat.T)
    print_mat(mat)
table_gen(16)
table_gen(32)