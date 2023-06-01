from sklearn.metrics.pairwise import cosine_similarity
import numpy as np


def compute_similarity(faceprint_arry1, faceprint_arry2):
    # Create faceprint vectors
    faceprint1 = np.array(faceprint_arry1)
    faceprint2 = np.array(faceprint_arry2)

    # Reshape the vectors
    faceprint1 = faceprint1.reshape(1, -1)
    faceprint2 = faceprint2.reshape(1, -1)

    # Compute the cosine similarity
    cos_sim = cosine_similarity(faceprint1, faceprint2)

    # print("Cosine similarity:", cos_sim[0][0])
    return cos_sim[0][0]
